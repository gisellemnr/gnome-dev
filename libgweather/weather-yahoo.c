/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* weather-yahoo.c - Yahoo! Weather service.
 *
 * Copyright 2012 Giovanni Campagna <scampa.giovanni@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE /* for strptime */
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <glib.h>

#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#define GWEATHER_I_KNOW_THIS_IS_UNSTABLE
#include "weather-priv.h"

#define XC(t) ((const xmlChar *)(t))

// FIXME: the definition of which conditions are set is somewhat arbitrary and
// characteristics such as 'isolated' and 'scattered' are not expressed with the
// current data structure.
static GWeatherConditions condition_codes[] = {
    /* tornado */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_TORNADO},
    /* tropical storm */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_THUNDERSTORM, 
      GWEATHER_PRECIPITATION_RAIN, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* hurricane */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_SHOWERS, 
      GWEATHER_PRECIPITATION_RAIN, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_TORNADO},
    /* severe thunderstorms */
    { TRUE, GWEATHER_INTENSITY_HEAVY, GWEATHER_DESCRIPTOR_THUNDERSTORM, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* thunderstorms FIXME: same as tropical storm */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_THUNDERSTORM, 
      GWEATHER_PRECIPITATION_RAIN, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* mixed rain and snow FIXME: once precipitation is a list, this can be fixed */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_RAIN, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* mixed rain and sleet FIXME: once precipitation is a list, this can be fixed */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_ICE_PELLETS, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* mixed snow and sleet FIXME: once precipitation is a list, this can be fixed */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_SNOW, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* freezing drizzle */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_FREEZING, 
      GWEATHER_PRECIPITATION_DRIZZLE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* drizzle */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_DRIZZLE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* freezing rain */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_FREEZING, 
      GWEATHER_PRECIPITATION_RAIN, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* showers */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_SHOWERS, 
      GWEATHER_PRECIPITATION_RAIN, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* showers */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_SHOWERS, 
      GWEATHER_PRECIPITATION_RAIN, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* snow flurries */
    { TRUE, GWEATHER_INTENSITY_LIGHT, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_SNOW, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* light snow showers */
    { TRUE, GWEATHER_INTENSITY_LIGHT, GWEATHER_DESCRIPTOR_SHOWERS, 
      GWEATHER_PRECIPITATION_SNOW, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* blowing snow */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_BLOWING, 
      GWEATHER_PRECIPITATION_SNOW, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* snow */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_SNOW, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* hail */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_HAIL, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* sleet */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_ICE_PELLETS, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* dust */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_WIDESPREAD_DUST, GWEATHER_OTHER_NONE},
    /* foggy */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_FOG, GWEATHER_OTHER_NONE},
    /* haze */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_HAZE, GWEATHER_OTHER_NONE},
    /* smoky */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_SMOKE, GWEATHER_OTHER_NONE},
    /* blustery */
    { TRUE, GWEATHER_INTENSITY_HEAVY, GWEATHER_DESCRIPTOR_BLOWING, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* windy */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_BLOWING, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* cold */
    { FALSE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* cloudy */
    { FALSE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* mostly cloudy (night) */
    { FALSE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* mostly cloudy (day) */
    { FALSE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* partly cloudy (night) */
    { FALSE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* partly cloudy (day) */
    { FALSE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* clear (night) */
    { FALSE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* sunny */
    { FALSE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* fair (night) */
    { FALSE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* fair (day) */
    { FALSE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},    
    /* mixed rain and hail FIXME once precipitation is a list, this can be fixed */
    { TRUE, GWEATHER_INTENSITY_LIGHT, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_HAIL, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* hot */
    { FALSE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* isolated thunderstorms */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_THUNDERSTORM, 
      GWEATHER_PRECIPITATION_RAIN, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* scattered thunderstorms */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_THUNDERSTORM, 
      GWEATHER_PRECIPITATION_RAIN, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* scattered thunderstorms */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_THUNDERSTORM, 
      GWEATHER_PRECIPITATION_RAIN, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* scattered showers */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_SHOWERS, 
      GWEATHER_PRECIPITATION_RAIN, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* heavy snow */
    { TRUE, GWEATHER_INTENSITY_HEAVY, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_SNOW, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* scattered snow showers */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_SHOWERS, 
      GWEATHER_PRECIPITATION_SNOW, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* heavy snow */
    { TRUE, GWEATHER_INTENSITY_HEAVY, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_SNOW, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* partly cloudy */
    { FALSE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_NONE, 
      GWEATHER_PRECIPITATION_NONE, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* thundershowers */
    { TRUE, GWEATHER_INTENSITY_HEAVY, GWEATHER_DESCRIPTOR_SHOWERS, 
      GWEATHER_PRECIPITATION_RAIN, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* snow showers */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_SHOWERS, 
      GWEATHER_PRECIPITATION_SNOW, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
    /* isolated thundershowers */
    { TRUE, GWEATHER_INTENSITY_NONE, GWEATHER_DESCRIPTOR_SHOWERS, 
      GWEATHER_PRECIPITATION_RAIN, GWEATHER_OBSCURATION_NONE, GWEATHER_OTHER_NONE},
};

/* FIXME: check sky values for codes that have a phenomenon too
   (scattered is what weather-iwin.c does for rain and snow)
*/
static GWeatherSky sky_codes[] = {
    GWEATHER_SKY_INVALID, /* tornado */
    GWEATHER_SKY_SCATTERED, /* tropical storm */
    GWEATHER_SKY_SCATTERED, /* hurricane */
    GWEATHER_SKY_SCATTERED, /* severe thunderstorms */
    GWEATHER_SKY_SCATTERED, /* thunderstorms */
    GWEATHER_SKY_SCATTERED, /* mixed rain and snow */
    GWEATHER_SKY_SCATTERED, /* mixed rain and sleet */
    GWEATHER_SKY_SCATTERED, /* mixed snow and sleet */
    GWEATHER_SKY_SCATTERED, /* freezing drizzle */
    GWEATHER_SKY_SCATTERED, /* drizzle */
    GWEATHER_SKY_SCATTERED, /* freezing rain */
    GWEATHER_SKY_SCATTERED, /* showers */
    GWEATHER_SKY_SCATTERED, /* showers */
    GWEATHER_SKY_SCATTERED, /* snow flurries */
    GWEATHER_SKY_SCATTERED, /* light snow showers */
    GWEATHER_SKY_SCATTERED, /* blowing snow */
    GWEATHER_SKY_SCATTERED, /* snow */
    GWEATHER_SKY_INVALID, /* hail */
    GWEATHER_SKY_INVALID, /* sleet */
    GWEATHER_SKY_INVALID, /* dust */
    GWEATHER_SKY_INVALID, /* foggy */
    GWEATHER_SKY_INVALID, /* haze */
    GWEATHER_SKY_INVALID, /* smoky */
    GWEATHER_SKY_INVALID, /* blustery */
    GWEATHER_SKY_INVALID, /* windy */
    GWEATHER_SKY_CLEAR, /* cold */
    GWEATHER_SKY_OVERCAST, /* cloudy */
    GWEATHER_SKY_FEW, /* mostly cloudy (night) */
    GWEATHER_SKY_FEW, /* mostly cloudy (day) */
    GWEATHER_SKY_BROKEN, /* partly cloudy (night) */
    GWEATHER_SKY_BROKEN, /* partly cloudy (day) */
    GWEATHER_SKY_CLEAR, /* clear (night) */
    GWEATHER_SKY_CLEAR, /* sunny */
    GWEATHER_SKY_CLEAR, /* fair (night) */
    GWEATHER_SKY_CLEAR, /* fair (day) */
    GWEATHER_SKY_SCATTERED, /* mixed rain and hail */
    GWEATHER_SKY_CLEAR, /* hot */
    GWEATHER_SKY_SCATTERED, /* isolated thunderstorms */
    GWEATHER_SKY_SCATTERED, /* scattered thunderstorms */
    GWEATHER_SKY_SCATTERED, /* scattered thunderstorms */
    GWEATHER_SKY_SCATTERED, /* scattered showers */
    GWEATHER_SKY_SCATTERED, /* heavy snow */
    GWEATHER_SKY_SCATTERED, /* scattered snow showers */
    GWEATHER_SKY_SCATTERED, /* heavy snow */
    GWEATHER_SKY_BROKEN, /* partly cloudy */
    GWEATHER_SKY_SCATTERED, /* thundershowers */
    GWEATHER_SKY_SCATTERED, /* snow showers */
    GWEATHER_SKY_SCATTERED, /* isolated thundershowers */
};

G_STATIC_ASSERT (G_N_ELEMENTS(condition_codes) == G_N_ELEMENTS(sky_codes));

static time_t
date_to_time_t (const xmlChar *str)
{
    struct tm time = { 0 };

    if (!strptime ((const char*) str, "%d %b %Y", &time)) {
	g_warning ("Cannot parse date string \"%s\"", str);
	return 0;
    }

    return mktime(&time);
}

static GWeatherInfo *
make_info_from_node (GWeatherInfo *master_info,
		     xmlNodePtr    node)
{
    GWeatherInfo *info;
    GWeatherInfoPrivate *priv;
    xmlChar *val;
    int code;

    g_return_val_if_fail (node->type == XML_ELEMENT_NODE, NULL);

    info = _gweather_info_new_clone (master_info);
    priv = info->priv;

    val = xmlGetProp (node, XC("date"));
    priv->current_time = priv->update = date_to_time_t (val);
    xmlFree (val);

    val = xmlGetProp (node, XC("high"));
    priv->temp_max = g_ascii_strtod ((const char*) val, NULL);
    xmlFree (val);

    val = xmlGetProp (node, XC("low"));
    priv->temp_min = g_ascii_strtod ((const char*) val, NULL);
    xmlFree (val);

    priv->tempMinMaxValid = priv->tempMinMaxValid || (priv->temp_max > -999.0 && priv->temp_min > -999.0);
    priv->valid = priv->tempMinMaxValid;

    val = xmlGetProp (node, XC("code"));
    code = strtol((const char*) val, NULL, 0);
    if (code >= 0 && code < G_N_ELEMENTS (condition_codes)) {
	    priv->cond = condition_codes[code];
	    priv->sky = sky_codes[code];
    } else
    	priv->valid = FALSE;
    
    xmlFree (val);

    return info;
}

static void
parse_forecast_xml (GWeatherInfo    *master_info,
		    SoupMessageBody *body)
{
    GWeatherInfoPrivate *priv;
    xmlDocPtr doc;
    xmlXPathContextPtr xpath_ctx;
    xmlXPathObjectPtr xpath_result;
    int i;

    priv = master_info->priv;

    doc = xmlParseMemory (body->data, body->length);
    if (!doc)
	return;

    xpath_ctx = xmlXPathNewContext (doc);
    xmlXPathRegisterNs (xpath_ctx, XC("yweather"), XC("http://xml.weather.yahoo.com/ns/rss/1.0"));
    xpath_result = xmlXPathEval (XC("/rss/channel/item/yweather:forecast"), xpath_ctx);

    if (!xpath_result || xpath_result->type != XPATH_NODESET)
	goto out;

    for (i = 0; i < xpath_result->nodesetval->nodeNr; i++) {
	xmlNodePtr node;
	GWeatherInfo *info;

	node = xpath_result->nodesetval->nodeTab[i];
	info = make_info_from_node (master_info, node);

	priv->forecast_list = g_slist_append (priv->forecast_list, info);
    }

    xmlXPathFreeObject (xpath_result);

 out:
    xmlXPathFreeContext (xpath_ctx);
    xmlFreeDoc (doc);
}

static void
yahoo_finish (SoupSession *session,
	      SoupMessage *msg,
	      gpointer     user_data)
{
    GWeatherInfo *info = GWEATHER_INFO (user_data);

    if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
	/* forecast data is not really interesting anyway ;) */
	if (msg->status_code != SOUP_STATUS_CANCELLED)
	    g_warning ("Failed to get Yahoo! Weather forecast data: %d %s\n",
		       msg->status_code, msg->reason_phrase);
	_gweather_info_request_done (info, msg);
	return;
    }

    parse_forecast_xml (info, msg->response_body);
    _gweather_info_request_done (info, msg);
}

gboolean
yahoo_start_open (GWeatherInfo *info)
{
    GWeatherInfoPrivate *priv;
    WeatherLocation *loc;
    gchar *url;
    SoupMessage *message;

    priv = info->priv;
    loc = &priv->location;

    if (!loc->yahoo_id)
	return FALSE;

    /* u=f means that the values are in imperial system (which is what
       weather.c expects). They're converted to user preferences before
       displaying.
    */
    url = g_strdup_printf("http://weather.yahooapis.com/forecastrss?w=%s&u=f", loc->yahoo_id);

    message = soup_message_new ("GET", url);
    _gweather_info_begin_request (info, message);
    soup_session_queue_message (priv->session, message, yahoo_finish, info);

    g_free (url);

    return TRUE;
}

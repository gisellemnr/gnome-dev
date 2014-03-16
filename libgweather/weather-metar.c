/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* weather-metar.c - Weather server functions (METAR)
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

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <regex.h>

#define GWEATHER_I_KNOW_THIS_IS_UNSTABLE
#include "weather-priv.h"

enum {
    TIME_RE,
    WIND_RE,
    VIS_RE,
    COND_RE,
    CLOUD_RE,
    TEMP_RE,
    PRES_RE,

    RE_NUM
};

enum {
    COND_INTE_RE,
    COND_DESC_RE,
    COND_PREC_RE,
    COND_OBSC_RE,
    COND_OTHR_RE,

    COND_RE_NUM
};

/* Return time of weather report as secs since epoch UTC */
static time_t
make_time (gint utcDate, gint utcHour, gint utcMin)
{
    const time_t now = time (NULL);
    struct tm tm;

    localtime_r (&now, &tm);

    /* If last reading took place just before midnight UTC on the
     * first, adjust the date downward to allow for the month
     * change-over.  This ASSUMES that the reading won't be more than
     * 24 hrs old! */
    if ((utcDate > tm.tm_mday) && (tm.tm_mday == 1)) {
        tm.tm_mday = 0; /* mktime knows this is the last day of the previous
			 * month. */
    } else {
        tm.tm_mday = utcDate;
    }
    tm.tm_hour = utcHour;
    tm.tm_min  = utcMin;
    tm.tm_sec  = 0;

    /* mktime() assumes value is local, not UTC.  Use tm_gmtoff to compensate */
#ifdef HAVE_TM_TM_GMOFF
    return tm.tm_gmtoff + mktime (&tm);
#elif defined HAVE_TIMEZONE
    return timezone + mktime (&tm);
#endif
}

static void
metar_tok_time (gchar *tokp, GWeatherInfo *info)
{
    gint day, hr, min;

    sscanf (tokp, "%2u%2u%2u", &day, &hr, &min);
    info->priv->update = make_time (day, hr, min);
}

static void
metar_tok_wind (gchar *tokp, GWeatherInfo *info)
{
    GWeatherInfoPrivate *priv;
    gchar sdir[4], sspd[4], sgust[4];
    gint dir, spd = -1;
    gchar *gustp;
    size_t glen;

    priv = info->priv;

    strncpy (sdir, tokp, 3);
    sdir[3] = 0;
    dir = (!strcmp (sdir, "VRB")) ? -1 : atoi (sdir);

    memset (sspd, 0, sizeof (sspd));
    glen = strspn (tokp + 3, CONST_DIGITS);
    strncpy (sspd, tokp + 3, glen);
    spd = atoi (sspd);
    tokp += glen + 3;

    gustp = strchr (tokp, 'G');
    if (gustp) {
        memset (sgust, 0, sizeof (sgust));
	glen = strspn (gustp + 1, CONST_DIGITS);
        strncpy (sgust, gustp + 1, glen);
	tokp = gustp + 1 + glen;
    }

    if (!strcmp (tokp, "MPS"))
	priv->windspeed = WINDSPEED_MS_TO_KNOTS ((GWeatherWindSpeed)spd);
    else
	priv->windspeed = (GWeatherWindSpeed)spd;

    if ((349 <= dir) || (dir <= 11))
        priv->wind = GWEATHER_WIND_N;
    else if ((12 <= dir) && (dir <= 33))
        priv->wind = GWEATHER_WIND_NNE;
    else if ((34 <= dir) && (dir <= 56))
        priv->wind = GWEATHER_WIND_NE;
    else if ((57 <= dir) && (dir <= 78))
        priv->wind = GWEATHER_WIND_ENE;
    else if ((79 <= dir) && (dir <= 101))
        priv->wind = GWEATHER_WIND_E;
    else if ((102 <= dir) && (dir <= 123))
        priv->wind = GWEATHER_WIND_ESE;
    else if ((124 <= dir) && (dir <= 146))
        priv->wind = GWEATHER_WIND_SE;
    else if ((147 <= dir) && (dir <= 168))
        priv->wind = GWEATHER_WIND_SSE;
    else if ((169 <= dir) && (dir <= 191))
        priv->wind = GWEATHER_WIND_S;
    else if ((192 <= dir) && (dir <= 213))
        priv->wind = GWEATHER_WIND_SSW;
    else if ((214 <= dir) && (dir <= 236))
        priv->wind = GWEATHER_WIND_SW;
    else if ((237 <= dir) && (dir <= 258))
        priv->wind = GWEATHER_WIND_WSW;
    else if ((259 <= dir) && (dir <= 281))
        priv->wind = GWEATHER_WIND_W;
    else if ((282 <= dir) && (dir <= 303))
        priv->wind = GWEATHER_WIND_WNW;
    else if ((304 <= dir) && (dir <= 326))
        priv->wind = GWEATHER_WIND_NW;
    else if ((327 <= dir) && (dir <= 348))
        priv->wind = GWEATHER_WIND_NNW;
}

static void
metar_tok_vis (gchar *tokp, GWeatherInfo *info)
{
    GWeatherInfoPrivate *priv;
    gchar *pfrac, *pend, *psp;
    gchar sval[6];
    gint num, den, val;

    priv = info->priv;

    memset (sval, 0, sizeof (sval));

    if (!strcmp (tokp,"CAVOK")) {
        // "Ceiling And Visibility OK": visibility >= 10 KM
        priv->visibility=10000. / VISIBILITY_SM_TO_M (1.);
        priv->sky = GWEATHER_SKY_CLEAR;
    } else if (0 != (pend = strstr (tokp, "SM"))) {
        // US observation: field ends with "SM"
        pfrac = strchr (tokp, '/');
        if (pfrac) {
	    if (*tokp == 'M') {
	        priv->visibility = 0.001;
	    } else {
	        num = (*(pfrac - 1) - '0');
		strncpy (sval, pfrac + 1, pend - pfrac - 1);
		den = atoi (sval);
		priv->visibility =
		    ((GWeatherVisibility)num / ((GWeatherVisibility)den));

		psp = strchr (tokp, ' ');
		if (psp) {
		    *psp = '\0';
		    val = atoi (tokp);
		    priv->visibility += (GWeatherVisibility)val;
		}
	    }
	} else {
	    strncpy (sval, tokp, pend - tokp);
            val = atoi (sval);
            priv->visibility = (GWeatherVisibility)val;
	}
    } else {
        // International observation: NNNN(DD NNNNDD)?
        // For now: use only the minimum visibility and ignore its direction
        strncpy (sval, tokp, strspn (tokp, CONST_DIGITS));
	val = atoi (sval);
	priv->visibility = (GWeatherVisibility)val / VISIBILITY_SM_TO_M (1.);
    }
}

static void
metar_tok_cloud (gchar *tokp, GWeatherInfo *info)
{
    GWeatherInfoPrivate *priv;
    gchar stype[4], salt[4];

    priv = info->priv;

    strncpy (stype, tokp, 3);
    stype[3] = 0;
    if (strlen (tokp) == 6) {
        strncpy (salt, tokp + 3, 3);
        salt[3] = 0;
    }

    if (!strcmp (stype, "CLR")) {
        priv->sky = GWEATHER_SKY_CLEAR;
    } else if (!strcmp (stype, "SKC")) {
        priv->sky = GWEATHER_SKY_CLEAR;
    } else if (!strcmp (stype, "NSC")) {
        priv->sky = GWEATHER_SKY_CLEAR;
    } else if (!strcmp (stype, "BKN")) {
        priv->sky = GWEATHER_SKY_BROKEN;
    } else if (!strcmp (stype, "SCT")) {
        priv->sky = GWEATHER_SKY_SCATTERED;
    } else if (!strcmp (stype, "FEW")) {
        priv->sky = GWEATHER_SKY_FEW;
    } else if (!strcmp (stype, "OVC")) {
        priv->sky = GWEATHER_SKY_OVERCAST;
    }
}

static void
metar_tok_pres (gchar *tokp, GWeatherInfo *info)
{
    GWeatherInfoPrivate *priv = info->priv;

    if (*tokp == 'A') {
        gchar sintg[3], sfract[3];
        gint intg, fract;

        strncpy (sintg, tokp + 1, 2);
        sintg[2] = 0;
        intg = atoi (sintg);

        strncpy (sfract, tokp + 3, 2);
        sfract[2] = 0;
        fract = atoi (sfract);

        priv->pressure = (GWeatherPressure)intg + (((GWeatherPressure)fract)/100.0);
    } else {  /* *tokp == 'Q' */
        gchar spres[5];
        gint pres;

        strncpy (spres, tokp + 1, 4);
        spres[4] = 0;
        pres = atoi (spres);

        priv->pressure = PRESSURE_MBAR_TO_INCH ((GWeatherPressure)pres);
    }
}

static void
metar_tok_temp (gchar *tokp, GWeatherInfo *info)
{
    GWeatherInfoPrivate *priv;
    gchar *ptemp, *pdew, *psep;

    priv = info->priv;

    psep = strchr (tokp, '/');
    *psep = 0;
    ptemp = tokp;
    pdew = psep + 1;

    priv->temp = (*ptemp == 'M') ? TEMP_C_TO_F (-atoi (ptemp + 1))
	: TEMP_C_TO_F (atoi (ptemp));
    if (*pdew) {
	priv->dew = (*pdew == 'M') ? TEMP_C_TO_F (-atoi (pdew + 1))
	    : TEMP_C_TO_F (atoi (pdew));
    } else {
	priv->dew = -1000.0;
    }
}

/* How "important" are the conditions to be reported to the user.
   Indexed by GWeatherConditionPrecipitation */
static const int importance_scale_precipitation[] = {
  0, /* invalid */
  0, /* none */
  20, /* drizzle */
  30, /* rain */
  35, /* snow */
  35, /* snow grains */
  35, /* ice crystals */
  35, /* ice pellets */
  35, /* hail */
  35, /* small hail */
  20, /* unknown */
};
/* Indexed by GWeatherConditionObscuration */
static const int importance_scale_obscuration[] = {
  0, /* invalid */
  0, /* none */
  10, /* mist */
  15, /* fog */
  15, /* smoke */
  18, /* volcanic ash */
  15, /* widespread dust */
  18, /* sand */
  15, /* haze */
  15, /* spray */
};
/* Indexed by GWeatherConditionOther */
static const int importance_scale_other[] = {
  0, /* invalid */
  0, /* none */
  50, /* dust whirls */
  40, /* squall */
  50, /* sandstorm */
  70, /* funnel cloud */
  70, /* tornado */
  50, /* duststorm */
};

static gboolean
condition_more_important (GWeatherConditions *which,
			  GWeatherConditions *than)
{
    if (!than->significant)
	    return TRUE;
    if (!which->significant)
	    return FALSE;

    // Other > Precipitation > Obscuration
    if (importance_scale_other[which->other] > importance_scale_other[than->other]) 
      return TRUE;
    else if (importance_scale_other[which->other] == importance_scale_other[than->other]) {
      if (importance_scale_precipitation[which->precipitation] > importance_scale_precipitation[than->precipitation])
        return TRUE;
      else if (importance_scale_precipitation[which->precipitation] == importance_scale_precipitation[than->precipitation]) {
        if (importance_scale_obscuration[which->obscuration] > importance_scale_obscuration[than->obscuration])
          return TRUE;
        else return FALSE;
      }
      else return FALSE;
    }
    else return FALSE;

    return FALSE;
}

// According to: http://weather.unisys.com/wxp/Appendices/Formats/METAR.html and wikipedia :)
#define COND_INTE_RE_STR "(-|\\+|VC|)"
#define COND_DESC_RE_STR "(MI|PR|BC|DR|BL|SH|TS|FZ)"
#define COND_PREC_RE_STR "(DZ|RA|SN|SG|IC|PL|GR|GS|UP)"
#define COND_OBSC_RE_STR "(BR|FG|FU|VA|DU|SA|HZ|PY)"
#define COND_OTHR_RE_STR "(PO|SQ|\\+?FC|SS|DS)"
#define COND_RE_STR  COND_INTE_RE_STR"?"COND_DESC_RE_STR"?"COND_PREC_RE_STR"?"COND_OBSC_RE_STR"?"COND_OTHR_RE_STR"?"

static void
metar_tok_cond (gchar *tokp, GWeatherInfo *info)
{
    GWeatherInfoPrivate *priv;
    GWeatherConditions new_cond;
    gchar substr[12], intensity[3], descriptor[3], precipitation[3], obscuration[3], other[4];
    regex_t cond_re[COND_RE_NUM];
    regmatch_t cond_rm[COND_RE_NUM];
    gint i, start;
    
    //printf("***** tokp: %s\n", tokp);
    
    regcomp (&cond_re[COND_INTE_RE], COND_INTE_RE_STR, REG_EXTENDED);
    regcomp (&cond_re[COND_DESC_RE], COND_DESC_RE_STR, REG_EXTENDED);
    regcomp (&cond_re[COND_PREC_RE], COND_PREC_RE_STR, REG_EXTENDED);
    regcomp (&cond_re[COND_OBSC_RE], COND_OBSC_RE_STR, REG_EXTENDED);
    regcomp (&cond_re[COND_OTHR_RE], COND_OTHR_RE_STR, REG_EXTENDED);

    start = 0;
    for(i = 0; i < COND_RE_NUM; i++) {
      strcpy(substr, tokp + start); 
      if(regexec(&cond_re[i], substr, 1, &cond_rm[i], 0) == 0) {
        cond_rm[i].rm_so += start;
        cond_rm[i].rm_eo += start;
        start = cond_rm[i].rm_eo + 1;
      }
    }

    priv = info->priv;
    
    // rm_so and rm_eo are -1 in case of no match.

    if (cond_rm[COND_INTE_RE].rm_so != -1) {
      strncpy(intensity, tokp + cond_rm[COND_INTE_RE].rm_so, cond_rm[COND_INTE_RE].rm_eo);
      if ( !strcmp(intensity, "-") ) 
        new_cond.intensity = GWEATHER_INTENSITY_LIGHT;
      else if ( !strcmp(intensity, "") )
        new_cond.intensity = GWEATHER_INTENSITY_MODERATE;
      else if ( !strcmp(intensity, "+") ) 
        new_cond.intensity = GWEATHER_INTENSITY_HEAVY;
      else if ( !strcmp(intensity, "VC") )
        new_cond.intensity = GWEATHER_INTENSITY_VICINITY;
      else return;
    } else {
      new_cond.intensity = GWEATHER_INTENSITY_NONE;
    }

    if (cond_rm[COND_DESC_RE].rm_so != -1) {
      strncpy(descriptor, tokp + cond_rm[COND_DESC_RE].rm_so, cond_rm[COND_DESC_RE].rm_eo);
      if ( !strcmp(descriptor, "MI") )
        new_cond.descriptor = GWEATHER_DESCRIPTOR_SHALLOW;
      else if ( !strcmp(descriptor, "PR") )
        new_cond.descriptor = GWEATHER_DESCRIPTOR_PARTIAL;
      else if ( !strcmp(descriptor, "BC") )
        new_cond.descriptor = GWEATHER_DESCRIPTOR_PATCHES;
      else if ( !strcmp(descriptor, "DR") )
        new_cond.descriptor = GWEATHER_DESCRIPTOR_LOW_DRIFTING;
      else if ( !strcmp(descriptor, "BL") )
        new_cond.descriptor = GWEATHER_DESCRIPTOR_BLOWING;
      else if ( !strcmp(descriptor, "SH") )
        new_cond.descriptor = GWEATHER_DESCRIPTOR_SHOWERS;
      else if ( !strcmp(descriptor, "TS") )
        new_cond.descriptor = GWEATHER_DESCRIPTOR_THUNDERSTORM;
      else if ( !strcmp(descriptor, "FZ") )
        new_cond.descriptor = GWEATHER_DESCRIPTOR_FREEZING;
      else return;
    } else {
      new_cond.descriptor = GWEATHER_DESCRIPTOR_NONE;
    }

    if (cond_rm[COND_PREC_RE].rm_so != -1) {
      strncpy(precipitation, tokp + cond_rm[COND_PREC_RE].rm_so, cond_rm[COND_PREC_RE].rm_eo);
      if ( !strcmp(precipitation, "DZ") ) 
        new_cond.precipitation = GWEATHER_PRECIPITATION_DRIZZLE;
      else if ( !strcmp(precipitation, "RA") )
        new_cond.precipitation = GWEATHER_PRECIPITATION_RAIN;
      else if ( !strcmp(precipitation, "SN") )
        new_cond.precipitation = GWEATHER_PRECIPITATION_SNOW;
      else if ( !strcmp(precipitation, "SG") )
        new_cond.precipitation = GWEATHER_PRECIPITATION_SNOW_GRAINS;
      else if ( !strcmp(precipitation, "IC") )
        new_cond.precipitation = GWEATHER_PRECIPITATION_ICE_CRYSTALS;
      else if ( !strcmp(precipitation, "PL") )
        new_cond.precipitation = GWEATHER_PRECIPITATION_ICE_PELLETS;
      else if ( !strcmp(precipitation, "GR") )
        new_cond.precipitation = GWEATHER_PRECIPITATION_HAIL;
      else if ( !strcmp(precipitation, "GS") )
        new_cond.precipitation = GWEATHER_PRECIPITATION_SMALL_HAIL;
      else if ( !strcmp(precipitation, "UP") )
        new_cond.precipitation = GWEATHER_PRECIPITATION_UNKNOWN;
      else return;
    } else {
      new_cond.precipitation = GWEATHER_PRECIPITATION_NONE;
    }

    if (cond_rm[COND_OBSC_RE].rm_so != -1) {
      strncpy(obscuration, tokp + cond_rm[COND_OBSC_RE].rm_so, cond_rm[COND_OBSC_RE].rm_eo);
      if ( !strcmp(obscuration, "BR") )
        new_cond.obscuration = GWEATHER_OBSCURATION_MIST;
      else if ( !strcmp(obscuration, "FG") )
        new_cond.obscuration = GWEATHER_OBSCURATION_FOG;
      else if ( !strcmp(obscuration, "FU") )
        new_cond.obscuration = GWEATHER_OBSCURATION_SMOKE;
      else if ( !strcmp(obscuration, "VA") )
        new_cond.obscuration = GWEATHER_OBSCURATION_VOLCANIC_ASH;
      else if ( !strcmp(obscuration, "DU") )
        new_cond.obscuration = GWEATHER_OBSCURATION_WIDESPREAD_DUST;
      else if ( !strcmp(obscuration, "SA") )
        new_cond.obscuration = GWEATHER_OBSCURATION_SAND;
      else if ( !strcmp(obscuration, "HZ") )
        new_cond.obscuration = GWEATHER_OBSCURATION_HAZE;
      else if ( !strcmp(obscuration, "PY") )
        new_cond.obscuration = GWEATHER_OBSCURATION_SPRAY;
      else return;
    } else {
      new_cond.obscuration = GWEATHER_OBSCURATION_NONE;
    }

    if (cond_rm[COND_OTHR_RE].rm_so != -1) {
      strncpy(other, tokp + cond_rm[COND_OTHR_RE].rm_so, cond_rm[COND_OTHR_RE].rm_eo);
      if( !strcmp(other, "PO") ) 
        new_cond.other = GWEATHER_OTHER_DUST_WHIRLS;
      else if( !strcmp(other, "SQ") ) 
        new_cond.other = GWEATHER_OTHER_SQUALL;
      else if( !strcmp(other, "+FC") ) 
        new_cond.other = GWEATHER_OTHER_TORNADO;
      else if( !strcmp(other, "FC") ) 
        new_cond.other = GWEATHER_OTHER_FUNNEL_CLOUD;
      else if( !strcmp(other, "SS") ) 
        new_cond.other = GWEATHER_OTHER_SANDSTORM;
      else if( !strcmp(other, "DS") ) 
        new_cond.other = GWEATHER_OTHER_DUSTSTORM;
      else return;
    } else {
      new_cond.other = GWEATHER_OTHER_NONE;
    }

    if ((new_cond.intensity != GWEATHER_INTENSITY_NONE) || 
        (new_cond.descriptor != GWEATHER_DESCRIPTOR_NONE) ||
        (new_cond.precipitation != GWEATHER_PRECIPITATION_NONE) ||
        (new_cond.obscuration != GWEATHER_OBSCURATION_NONE) ||
        (new_cond.other != GWEATHER_OTHER_NONE) )
        new_cond.significant = TRUE;

    if (condition_more_important (&new_cond, &priv->cond))
	    priv->cond = new_cond;
}

#define TIME_RE_STR  "([0-9]{6})Z"
#define WIND_RE_STR  "(([0-9]{3})|VRB)([0-9]?[0-9]{2})(G[0-9]?[0-9]{2})?(KT|MPS)"
#define VIS_RE_STR   "((([0-9]?[0-9])|(M?([12] )?([1357]/1?[0-9])))SM)|" \
    "([0-9]{4}(N|NE|E|SE|S|SW|W|NW( [0-9]{4}(N|NE|E|SE|S|SW|W|NW))?)?)|" \
    "CAVOK"
#define CLOUD_RE_STR "((CLR|BKN|SCT|FEW|OVC|SKC|NSC)([0-9]{3}|///)?(CB|TCU|///)?)"
#define TEMP_RE_STR  "(M?[0-9][0-9])/(M?(//|[0-9][0-9])?)"
#define PRES_RE_STR  "(A|Q)([0-9]{4})"

/* POSIX regular expressions do not allow us to express "match whole words
 * only" in a simple way, so we have to wrap them all into
 *   (^| )(...regex...)( |$)
 */
#define RE_PREFIX "(^| )("
#define RE_SUFFIX ")( |$)"

static regex_t metar_re[RE_NUM];
static void (*metar_f[RE_NUM]) (gchar *tokp, GWeatherInfo *info);

static void
metar_init_re (void)
{
    static gboolean initialized = FALSE;
    if (initialized)
        return;
    initialized = TRUE;

    regcomp (&metar_re[TIME_RE], RE_PREFIX TIME_RE_STR RE_SUFFIX, REG_EXTENDED);
    regcomp (&metar_re[WIND_RE], RE_PREFIX WIND_RE_STR RE_SUFFIX, REG_EXTENDED);
    regcomp (&metar_re[VIS_RE], RE_PREFIX VIS_RE_STR RE_SUFFIX, REG_EXTENDED);
    regcomp (&metar_re[COND_RE], RE_PREFIX COND_RE_STR RE_SUFFIX, REG_EXTENDED);
    regcomp (&metar_re[CLOUD_RE], RE_PREFIX CLOUD_RE_STR RE_SUFFIX, REG_EXTENDED);
    regcomp (&metar_re[TEMP_RE], RE_PREFIX TEMP_RE_STR RE_SUFFIX, REG_EXTENDED);
    regcomp (&metar_re[PRES_RE], RE_PREFIX PRES_RE_STR RE_SUFFIX, REG_EXTENDED);

    metar_f[TIME_RE] = metar_tok_time;
    metar_f[WIND_RE] = metar_tok_wind;
    metar_f[VIS_RE] = metar_tok_vis;
    metar_f[COND_RE] = metar_tok_cond;
    metar_f[CLOUD_RE] = metar_tok_cloud;
    metar_f[TEMP_RE] = metar_tok_temp;
    metar_f[PRES_RE] = metar_tok_pres;
}

gboolean
metar_parse (gchar *metar, GWeatherInfo *info)
{
    gchar *p;
    //gchar *rmk;
    gint i, i2;
    regmatch_t rm, rm2;
    gchar *tokp;

    g_return_val_if_fail (info != NULL, FALSE);
    g_return_val_if_fail (metar != NULL, FALSE);

    metar_init_re ();

    /*
     * Force parsing to end at "RMK" field.  This prevents a subtle
     * problem when info within the remark happens to match an earlier state
     * and, as a result, throws off all the remaining expression
     */
    if (0 != (p = strstr (metar, " RMK "))) {
        *p = '\0';
	      //rmk = p + 5;   // uncomment this if RMK data becomes useful
    }

    p = metar;
    i = TIME_RE;
    while (*p) {

      i2 = RE_NUM;
      rm2.rm_so = strlen (p);
      rm2.rm_eo = rm2.rm_so;

      for (i = 0; i < RE_NUM && rm2.rm_so > 0; i++) {
        if (0 == regexec (&metar_re[i], p, 1, &rm, 0)
            && rm.rm_so < rm2.rm_so)
        {
          i2 = i;
          /* Skip leading and trailing space characters, if present.
             (the regular expressions include those characters to
             only get matches limited to whole words). */
          if (p[rm.rm_so] == ' ') rm.rm_so++;
          if (p[rm.rm_eo - 1] == ' ') rm.rm_eo--;
          rm2.rm_so = rm.rm_so;
          rm2.rm_eo = rm.rm_eo;
        }
      }

      if (i2 != RE_NUM) {
        tokp = g_strndup (p + rm2.rm_so, rm2.rm_eo - rm2.rm_so);
        metar_f[i2] (tokp, info);
        g_free (tokp);
      }

      p += rm2.rm_eo;
      p += strspn (p, " ");
    }
    return TRUE;
}

static void
metar_finish (SoupSession *session, SoupMessage *msg, gpointer data)
{
    GWeatherInfo *info = (GWeatherInfo *)data;
    GWeatherInfoPrivate *priv;
    WeatherLocation *loc;
    const gchar *p, *eoln;
    gchar *searchkey, *metar;
    gboolean success = FALSE;

    g_return_if_fail (info != NULL);

    priv = info->priv;
   
    if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
	if (SOUP_STATUS_IS_TRANSPORT_ERROR (msg->status_code))
	    priv->network_error = TRUE;
	else {
	    /* Translators: %d is an error code, and %s the error string */
	    g_warning (_("Failed to get METAR data: %d %s.\n"),
		       msg->status_code, msg->reason_phrase);
	}

	_gweather_info_request_done (info);
	return;
    }

    loc = &priv->location;

    searchkey = g_strdup_printf ("\n%s", loc->code);
    p = strstr (msg->response_body->data, searchkey);
    g_free (searchkey);
    if (p) {
	p += WEATHER_LOCATION_CODE_LEN + 2;
	eoln = strchr(p, '\n');
	if (eoln)
	    metar = g_strndup (p, eoln - p);
	else
	    metar = g_strdup (p);
	success = metar_parse (metar, info);
	g_free (metar);
    } else if (!strstr (msg->response_body->data, "National Weather Service")) {
	/* The response doesn't even seem to have come from NWS...
	 * most likely it is a wifi hotspot login page. Call that a
	 * network error.
	 */
	priv->network_error = TRUE;
    }

    priv->valid = success;
    _gweather_info_request_done (info);
}

/* Read current conditions and fill in info structure */
void
metar_start_open (GWeatherInfo *info)
{
    GWeatherInfoPrivate *priv;
    WeatherLocation *loc;
    SoupMessage *msg;

    g_return_if_fail (info != NULL);

    priv = info->priv;

    priv->valid = priv->network_error = FALSE;
    loc = &priv->location;

    msg = soup_form_request_new (
	"GET", "http://weather.noaa.gov/cgi-bin/mgetmetar.pl",
	"cccc", loc->code,
	NULL);
    soup_session_queue_message (priv->session, msg, metar_finish, info);

    priv->requests_pending++;
}

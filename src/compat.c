
/*
 * compat.c -- functions that we need to provide because they are missing 
 */
/*
 * $Id: compat.c,v 1.2 1997/04/16 06:00:43 dpassmor Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"

#ifdef NEED_TIMELOCAL

#define	altzone		((time_t)(timezone + 3600))

/*
 * CAUTION: side effects 
 */
#define	is_leap(x)	(((!((x)%4)&&(x)%100)||!(((x)+1900)%400))?1:0)

static int yday_size[2][12] =
{
	{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334},
	{0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335}
};

static int mon_size[2][12] =
{
	{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
	{31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
};

/*
 * e.g: normalize(&tm_secs, &tm_hour, 60); 
 */
static void normalize(x, y, f)
int *x, *y, f;
{
	if (*x >= f) {
		*y += *x / f;
		*x %= f;
	}
	/*
	 * can't rely on modulus for negative numbers 
	 */

	while (*x < 0) {
		*x += f;
		(*y)--;
	}
}

time_t timelocal(tp)
struct tm *tp;
{
	long unsigned k;
	struct tm tm;
	time_t t, t1;

	tzset();		/*
				 * set up time zone 
				 */

	/*
	 * normalize the time 
	 */
	normalize(&tp->tm_sec, &tp->tm_min, 60);
	normalize(&tp->tm_min, &tp->tm_hour, 60);
	normalize(&tp->tm_hour, &tp->tm_mday, 24);

	/*
	 * normalize the month first 
	 */
	normalize(&tp->tm_mon, &tp->tm_year, 12);

	/*
	 * days to months a little tricky 
	 */
	while (tp->tm_mday < 1) {
		if (--tp->tm_mon < 0) {
			tp->tm_year--;
			tp->tm_mon += 12;
		}
		tp->tm_mday += mon_size[is_leap(tp->tm_year)][tp->tm_mon];
	}

	while (tp->tm_mday > (k = mon_size[is_leap(tp->tm_year)][tp->tm_mon])) {
		tp->tm_mday -= k;

		if (++tp->tm_mon > 12) {
			tp->tm_year++;
			tp->tm_mon -= 12;
		}
	}

	k = tp->tm_year / 4;	/*
				 * number of 4 year groups 
				 */
	t = (k * 1461) - 1;	/*
				 * number of days 
				 */
	k = tp->tm_year % 4;	/*
				 * number of years beyond group 
				 */
	t += (k * 365);		/*
				 * add number of days 
				 */
	if (k)			/*
				 * * if not group break year  
				 */
		t++;		/*
				 * add one day 
				 */

	/*
	 * Since the epoch starts at Jan 1/70, we have to subtract the * of * 
	 * 
	 * *  * *  * *  * * days from Jan 1/00.  This is actually 25567 days. 
	 * See  * * * below * for  *  * * the explanation of the discrepancy 
	 */

	t -= 25568;		/*
				 * t = # days to 00:00:00 Jan 1/70 
				 */
	t += yday_size[is_leap(tp->tm_year)][tp->tm_mon];

	/*
	 * Add the number of days in month.  Note that we should really * * * 
	 * 
	 * *  * *  * * subtract 1 from the day first but we effectively did
	 * this * * * above * * * when we subtracted an extra day (25568
	 * instead of * * 25567) 
	 */

	t += tp->tm_mday;	/*
				 * # days to given day at 00:00:00 
				 */

	/*
	 * now add in the number of seconds in the day 
	 */

	t = (t * 24) + tp->tm_hour;
	t = (t * 60) + tp->tm_min;
	t = (t * 60) + tp->tm_sec;	/*
					 * total number of seconds 
					 */

	/*
	 * if caller thinks he/she knows what time zone then believe them 
	 */

	if (tp->tm_isdst == 0)
		t += timezone;
	else if (tp->tm_isdst > 0)
		t += altzone;
	else {
		/*
		 * we have to figure it out, guess dst first 
		 */

		t1 = t + altzone;
		tm = *localtime(&t1);

		/*
		 * see if the guess matches the reality 
		 */

		if (tm.tm_hour == tp->tm_hour && tm.tm_min == tp->tm_min)
			t = t1;
		else {

#ifdef CHECK_INVALID

			/*
			 * if CHECK_INVALID is defined then we attempt to * * 
			 * 
			 * *  * *  * *  * * check for the invalid time case
			 * e.g. a  * time  * of *  * *  * 0230h on the first
			 * sunday in * April * will * return -1 * * * -
			 * personally I * don't think * this is  * polite * * 
			 * behaviour 
			 */

			t1 = t + timezone;
			tm = *localtime(&t1);

			if ((tm.tm_hour == tp->tm_hour) &&
			    (tm.tm_min == tp->tm_min))
				t = t1;
			else
				return (-1);
#else
			t += timezone;
#endif
		}
	}

	*tp = *localtime(&t);	/*
				 * set other fields in structure 
				 */
	return (t);
}

#endif /*
        * * NEED_TIMELOCAL  
        */

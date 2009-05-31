/*============================================================================
 * homeworld.c
 * Keeps make from bitching...
 *
 * Author:  Ted Cipicchio <ted@thereisnospork.com>
 * Created: Sat Oct 4 2003
 *==========================================================================*/

/* ... */
//temporary implemenation for missing devkitppc stuff
#ifdef GEKKO
#include <wctype.h>
int wcscasecmp(const wint_t *s1, const wint_t *s2)
{
    while(*s1 && *s2)
    {
	const wint_t wc1 = towupper(*s1);
	const wint_t wc2 = towupper(*s2);

        if(wc1 != wc2)
		return wc1 - wc2;

	s1++;
	s2++;
    }

    return towupper(*s1) - towupper(*s2);
}
#endif

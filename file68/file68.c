/*
 *                         sc68 - "sc68" file functions
 *         Copyright (C) 2001 Benjamin Gerard <ben@sashipa.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <config68.h>

#include <string.h>
#ifdef EMSCRIPTEN
#include <stdio.h>
#include <ctype.h>
#endif
#include "emu68/type68.h"
#include "file68/file68.h"
#include "file68/error68.h"
#include "file68/alloc68.h"
#include "file68/debugmsg68.h"

#include "file68/istream68_file.h"
#include "file68/istream68_mem.h"
#include "file68/gzip68.h"
#include "file68/ice68.h"

#ifdef EMSCRIPTEN
#define FOURCC(A,B,C,D) ((int)( ((A)<<24) | ((B)<<16) | ((C)<<8) | (D) ))
#define sndh_cc FOURCC('S','N','D','H')
#endif

/* SC68 file identifier string
 */
const char SC68file_idstr[] = SC68_IDSTR;

/* Peek Big Endian Unaligned 32 bit value */
static int LPeek(const void *a)
{
  int r;
  unsigned char *c = (unsigned char *) a;
  r = c[0] + (c[1] << 8) + (c[2] << 16) + (c[3] << 24);
  return r;
}

/* Poke Big Endian Unaligned 32 bit value */
static void LPoke(void *a, int r)
{
  unsigned char *c = (unsigned char *) a;
  c[0] = r;
  c[1] = r >> 8;
  c[2] = r >> 16;
  c[3] = r >> 24;
}
#ifdef EMSCRIPTEN
// improved version from 3.x development version
static inline int sndh_inst(int c) {
  return (c &0xFF00) == 0x6000          /* bra and bra.s */
  || c == 0x4efa                        /* jmp (pc) */
  || c == 0x4e75                        /* rts */
  || c == 0x4e71                        /* nop */
  ;
}

/* Peek big Endian Unaligned 16 bit value */
static inline int WPeekBE(const void * const a)
{
  int r;
  const unsigned char * const c = (const unsigned char *) a;
  r = c[1] + ((int)(signed char)c[0] << 8);
  return r;
}

/* Peek Big Endian Unaligned 32 bit value */
static inline int LPeekBE(const void * const a)
{
  int r;
  const unsigned char * const c = (const unsigned char *) a;
  r = ((int)(signed char)c[0] << 24) + (c[1] << 16) + (c[2] << 8) + c[3];
  return r;
}

static int sndh_is_magic(const char *buffer, int max)
{
  const int start = 10;
  int i=0, v = 0;
  if (max >= start
      && sndh_inst(WPeekBE(buffer+0))
      /* && sndh_inst(WPeekBE(buffer+4)) */
      && sndh_inst(WPeekBE(buffer+8))) {
    for (i=start, v = LPeekBE(buffer+i); i < max && v != sndh_cc;
         v = ((v<<8) | (buffer[i++]&255)) & 0xFFFFFFFF)
      ;
  }
  i = (v == sndh_cc) ? i-4: 0;
  return i;
}

/* Array of static strings, mainly used by metatags. */
static /* const */ struct strings_table {
  char n_a[4];
  char aka[4];
  char sc68[5];
  char sndh[5];
  char rate[5];
  char year[5];
  char album[6];
  char genre[6];
  char image[6];
  char title[6];
  char replay[7];
  char artist[7];
  char author[7];
  char ripper[7];
  char format[7];
  char comment[8];
  char composer[9];
  char converter[10];
  char copyright[10];
  char amiga_chiptune[15];
  char atari_st_chiptune[18];

  char _reserved;
} tagstr = {
  SC68_NOFILENAME,
  TAG68_AKA,
  "sc68",
  "sndh",
  TAG68_RATE,
  TAG68_YEAR,
  TAG68_ALBUM,
  TAG68_GENRE,
  TAG68_IMAGE,
  TAG68_TITLE,
  TAG68_REPLAY,
  TAG68_ARTIST,
  TAG68_AUTHOR,
  TAG68_RIPPER,
  TAG68_FORMAT,
  TAG68_COMMENT,
  TAG68_COMPOSER,
  TAG68_CONVERTER,
  TAG68_COPYRIGHT,
  "Amiga chiptune",
  "Atari-ST chiptune",
  -1,
};


#else
static int sndh_is_magic(const char *buffer, int max)
{
  const int start = 6;
  int i=0, v = 0;
  if (max >= start) {
    for (i=start, v = LPeek(buffer); i < max && v != 'SNDH';
	 v = ((v<<8)| (buffer[i++]&255)) & 0xFFFFFFFF)
      ;
  }
  i = (v == 'SNDH') ? i-4: 0;
  return i;
}
#endif

/* Verify header , return # byte to alloc & read
 * or -1 if error
 * or -'gzip' if may be gzipped
 * or -'ice!' if may be iced
 * or -'sndh' if may be sndh
 * or 
 */
static int read_header(istream_t * is)
{
  char id[256];
  const int l = sizeof(SC68file_idstr);
  int l2;

  /* Check ID string */
  if (istream_read(is, id, l) != l) {
    return SC68error_add("Not SC68 file : Missing ID");
  }

  if (memcmp(id, SC68file_idstr, l)) {
    if (gzip_is_magic(id)) {
      return -'gzip';
    } else if (ice_is_magic(id)) {
      return -'ice!';
    } else if (sndh_is_magic(id,l)) {
      /* Must be done after gzip or ice becoz id-string may appear in
       * compressed buffer too.
       */
      return -'sndh';
    }
    return SC68error_add("Not SC68 file : Missing ID");
  }

  /* Check 1st chunk */
  if (istream_read(is, id, 4) != 4
      || memcmp(id, CH68_CHUNK CH68_BASE, 4)) {
    return SC68error_add("Not SC68 file : Missing BASE Chunk");
  }

  /* Get base chunk : file total size */
  if (istream_read(is, id, 4) != 4
      || (l2 = LPeek(id), l2 <= 8)) {
    return SC68error_add("Not SC68 file : Weird BASE Chunk size");
  }
  return l2 - 8;
}

static char noname[] = SC68_NOFILENAME;


/* FR  = SEC * HZ
 * FR  = MS*HZ/1000
 *
 * SEC = FR / HZ
 * MS  = FR*1000/HZ
 */

static unsigned int frames_to_ms(unsigned int frames, unsigned int hz)
{
  u64 ms;

  ms = frames;
  ms *= 1000u;
  ms /= hz;

  return (unsigned int) ms;
}

static unsigned int ms_to_frames(unsigned int ms, unsigned int hz)
{
  u64 fr;

  fr =  ms;
  fr *= hz;
  fr /= 1000u;

  return (unsigned int ) fr;
}


/* This function init all pointers for this music files
 * It setup non initialized data to defaut value.
 */
static int valid(disk68_t * mb)
{
  music68_t *m;
  int i, previousdatasz = 0;
  void *previousdata = 0;
  char *author = noname;
  char *composer = 0;
  char *mname;

  if (mb->nb_six <= 0) {
    return SC68error_add("No music defined");
  }

  /* Ensure default music in valid range */
  if (mb->default_six < 0 || mb->default_six >= mb->nb_six) {
    mb->default_six = 0;
  }

  /* No name : set default */
  if (mb->name == 0) {
#ifdef EMSCRIPTEN	  
	// SNDH fallback
	mb->name= mb->mus[0].name;
#else
    mb->name = noname;
#endif
  }
  
  mname = mb->name;

  /* Disk total time : 00:00 */
  mb->time_ms = 0;

  /* Clear flags */
  mb->flags = 0;

  /* Init all music in this file */
  for (m = mb->mus, i = 0; m < mb->mus + mb->nb_six; m++, i++) {
    /* default load address */
    if (m->a0 == 0) {
      m->a0 = SC68_LOADADDR;
    }
    /* default replay frequency is 50Hz */
    if (m->frq == 0) {
      m->frq = 50;
    }

    /* Compute ms from frames prior to frames from ms. */ 
    if (m->frames) {
      m->time_ms = frames_to_ms(m->frames, m->frq);
    } else {
      m->frames = ms_to_frames(m->time_ms,m->frq);
    }

    /* Set start time in the disk. */
    m->start_ms = mb->time_ms;

    /* Advance disk total time. */
    mb->time_ms += m->time_ms;

    /* default mode is YM2149 (Atari ST) */
    if (*(int *) (&m->flags) == 0)
      m->flags.ym = 1;
    mb->flags |= *(int *) (&m->flags);
    /* default music name is file name */
    mname = m->name = (m->name == 0) ? mname : m->name;
    /* default author */
    author = m->author = (m->author == 0) ? author : m->author;
    /* default composer is author */
    composer = m->composer = (m->composer == 0) ?
	((composer == 0) ? m->author : composer) : m->composer;
    /* use data from previuous music */
    if (m->data == 0) {
      m->data = (char *) previousdata;
      m->datasz = previousdatasz;
    }
    if (m->data == 0)
      return SC68error_add("music #%d as no data", i);
    previousdata = m->data;
    previousdatasz = m->datasz;
  }

  return 0;
}


/* Check if file is probable SC68 file
 * return 0:SC68-file
 */
int SC68file_verify(istream_t * is)
{
  int res;
  const char * fname;

  if (istream_open(is) == -1) {
    return -1;
  }
  fname = istream_filename(is);

  res = read_header(is);

  /* Verify tells it is a gzip file, so we may give it a try. */
  if (res < 0) {
    void * buffer = 0;
    int len;
    
    switch (res) {
    case -'gzip':
      if (memcmp(fname, "mem://",6)) {
	buffer = gzip_load(fname, &len);
      }
      break;
    case -'ice!':
      debugmsg68("ICE VERIFY [%s]!\n", fname);
      if (istream_seek_to(is,0) == 0) {
	buffer = ice_load(is, &len);
	debugmsg68("-> %p %d\n", buffer, len);
      }
      break;
    case -'sndh':
      debugmsg68("SNDH OK!\n");
      res = 0;
      break;
    }
    if (buffer) {
      res = -res;
      debugmsg68 ("%s depack %c%c%c%c\n", fname, res>>24,res>>16,res>>8,res);
      res = SC68file_verify_mem(buffer, len);
      debugmsg68 ("-> %d %p %d\n",res, buffer, len);
      SC68free(buffer);
    }
  }
  return -(res < 0);
}

int SC68file_verify_file(const char * fname)
{
  int res;
  istream_t * is;

  is = istream_file_create(fname,1);
  res = SC68file_verify(is);
  istream_destroy(is);

  return -(res < 0);
}

int SC68file_verify_mem(const void * buffer, int len)
{
  int res;
  istream_t * is;

  is = istream_mem_create((void *)buffer,len,1);
  res = SC68file_verify(is);
  istream_destroy(is);

  return res;
}

/* Check if file is probable SC68 file
 * return 0:SC68-file
 */
int SC68file_diskname(istream_t * is, char *dest, int max)
{
  return -1;
}

#if 0
{
  FILE *f;
  chunk68_t chunk;
  int total, len;
  
  dest[0] = dest[max-1] = 0;
  
  if (f = SC68_fopen(name, "rb"), f == 0)
    return -1;
    
  total = len = read_header(f);
  while (len >= 0) {
  
    /* Check chunk */
    if (SC68_fread(&chunk, sizeof(chunk), f)) {
      len = SC68error_add("Can't read next chunk");
      break;
    }
    total -= sizeof(chunk);
    
    if (memcmp(chunk.id, CH68_CHUNK, 2)) {
      len =  SC68error_add("Invalid chunk");
      break;
    }
    len = LPeek(chunk.size);
    if (len < 0) {
      break;
    }
    
    if (memcmp(chunk.id+2, CH68_FNAME, 2)) {
      total -= len;
      if (SC68_fseek(f, len) < 0) {
        len = SC68error_add("Can't reach next chunk");
      }
    } else if (!memcmp(chunk.id+2, CH68_EOF, 2)) {
      total = -1;
    } else {
      if (len >= max) len = max-1;
      if (SC68_fread(dest, len, f) < 0) {
        len =  SC68error_add("Can't read disk filename");
      }
      break;
    }
    
    if (total <= 0) {
      len = SC68error_add( CH68_CHUNK CH68_FNAME " chunk not found");
    }
  }
  
  SC68_fclose(f);
  
  return len < 0 ? -1 : 0;
}
#endif

disk68_t * SC68file_load_file(const char * fname)
{
  disk68_t * d;
  istream_t * is;

  is = istream_file_create(fname,1);
  d = SC68file_load(is);
  istream_destroy(is);

  return d;
}

disk68_t * SC68file_load_mem(const void * buffer, int len)
{
  disk68_t * d;
  istream_t * is;

  is = istream_mem_create((void *)buffer,len,1);
  d = SC68file_load(is);
  istream_destroy(is);

  return d;
}

static int myatoi(const char *s, int i, int max, int * pv)
{
  int v = 0;
  for (; i<max; ++i) {
    int c = s[i] & 255;
    if (c>='0' && c<='9') {
      v = v * 10 + c - '0';
    } else {
      break;
    }
  }
  *pv = v;
  return i;
}

/* according to Ben the original 2.1.1 "sndh_info" version is buggy causing problems with compressed sndh files
 -> I therefore merged the improved version from 3.x development branch */
static int st_isupper( int c )
{
  return (c >= 'A' && c <= 'Z');
}

static int st_isdigit( int c )
{
  return c >= '0' && c <= '9';
}

static int st_istag( int c )
{
  return c == '!' || c == '#' || c == '*';
}

static int st_isgraph( int c )
{
  return st_isupper(c) || st_isdigit(c) || st_istag(c);
}
/* @see http://sndh.atari.org/fileformat.php  */
static int sndh_info(disk68_t * mb, int len)
{
  const int unknowns_max = 8;
  int i, vbl = 0, frq = 0/* , steonly = 0 */,
    unknowns = 0, fail = 0;
  int dtag = 0, ttag = 0;

  char * b = mb->data;
  char empty_tag[4] = { 0, 0, 0, 0 };

  /* Default */
  mb->mus[0].data   = b;
  mb->mus[0].datasz = len;

#if 1
  mb->nb_six = -1; /* Make validate failed */
#else
  mb->nb_six = 1; /* Assume default of 1 track */
#endif

  mb->mus[0].replay = 0;

  i = sndh_is_magic(b, len);
  if (!i) {
    /* should not happen since we already have tested it. */
    debugmsg68("file68: sndh -- info missing magic!\n");
    return -1;
  }

/*   i   += 4; /\* Skip sndh_cc *\/ */
/*   len -= 4; */

  /* $$$ Hacky:
     Some music have 0 after values. I don't know what are
     sndh rules. May be 0 must be skipped or may be tag must be word
     aligned.
     Anyway the current parser allows a given number of successive
     unknown tags. May be this number should be increase in order to prevent
     some "large" unknown tag to break the parser.
  */

  while (i+4 < len) {
    char ** p;
    int j, t, s, ctypes;

    /* check char types for the next 4 chars */
    for (ctypes = 0, j=0; j<4; ++j) {
      ctypes |= (st_isgraph( b[i+j] ) << j );
      ctypes |= (st_isdigit( b[i+j] ) << (j + 8) );
    }

    debugmsg68(
            "file68: sndh -- pos:%d/%d ctypes:%04X -- '%c%c%c%c'\n",
            i, len, ctypes, b[i+0], b[i+1], b[i+2], b[i+3]);

    t       = -1;                       /* offset on tag */
    s       = -1;                       /* offset on arg */
    p       = 0;                        /* store arg     */

    if (  (ctypes & 0x000F) != 0x000F ) {
      /* Not graphical ... should not be a valid tag */
    } else if (!memcmp(b+i,"SNDH",4)) {
      /* Header */
      t = i; i += 4;
    } else if (!memcmp(b+i,"COMM",4)) {
      /* Artist */
      t = i; s = i += 4;
      p = &mb->mus[0].tags.tag.artist.val;
    } else if (!memcmp(b+i,"TITL",4)) { /* title    */
      /* Title */
      t = i; s = i += 4;
      p = &mb->tags.tag.title.val;
    } else if (!memcmp(b+i,"RIPP",4)) {
      /* Ripper */
      if (ttag < TAG68_ID_CUSTOM_MAX) {
        t = i; s = i += 4;
        mb->mus[0].tags.tag.custom[ttag].key = tagstr.ripper;
        p = &mb->mus[0].tags.tag.custom[ttag++].val;
      }
    } else if (!memcmp(b+i,"CONV",4)) {
      /* Converter */
      if (ttag < TAG68_ID_CUSTOM_MAX) {
        t = i; s = i += 4;
        mb->mus[0].tags.tag.custom[ttag].key = tagstr.converter;
        p = &mb->mus[0].tags.tag.custom[ttag++].val;
      }
    } else if (!memcmp(b+i, "YEAR", 4)) {
      /* year */
      if (dtag < TAG68_ID_CUSTOM_MAX) {
        t = i; s = i += 4;
        mb->tags.tag.custom[dtag].key = tagstr.year;
        p = &mb->tags.tag.custom[dtag++].val;
      }
#if 0 /* Does not happen probably just a typo in the sndh format doc. */
    } else if ( (ctypes & 0x0F00) == 0x0F00 && is_year(b+i) && !b[4] ) {
      assert(0);
      /* match direct a YEAR */
      if (dtag < TAG68_ID_CUSTOM_MAX) {
        t = i; s = i += 4;
        mb->tags.tag.custom[dtag].key = tagstr.year;
        mb->tags.tag.custom[dtag++].val = b;
      }
#endif
    } else if (!memcmp(b+i,"MuMo",4)) {
      /* MusicMon ???  */
      debugmsg68("file68: sndh -- %s\n","what to do with 'MuMo' tag ?");
      /* musicmon = 1; */
      t = i; i += 4;
    } else if (!memcmp(b+i,"TIME",4)) {
      /* Time in second */
      int j, tracks = mb->nb_six <= 0 ? 1 : mb->nb_six;
      t = i; i += 4;
      for ( j = 0; j < tracks; ++j ) {
        if (i < len-2 && j < SC68_MAX_TRACK)
          mb->mus[j].time_ms = 1000u *
            ( ( ( (unsigned char) b[i]) << 8 ) | (unsigned char) b[i+1] );
        debugmsg68(
                "file68: sndh -- TIME #%02d -- 0x%02X%02X (%c%c) -- %u ms\n",
                j+1, (unsigned char)b[i], (unsigned char)b[i+1],
                isgraph((int)(unsigned char)b[i])?b[i]:'.',
                isgraph((int)(unsigned char)b[i+1])?b[i+1]:'.',
                mb->mus[j].time_ms);
        i += 2;
      }
    } else if (!memcmp(b+i, "FLAG", 4)) {
      /* Track features (hardware,fx...) */
      int j, max=0, tracks = mb->nb_six <= 0 ? 1 : mb->nb_six;
      t = i;
      for ( j = 0; j < tracks; ++j ) {
        music68_t * m = mb->mus+j;
        int k, off = WPeekBE(b + i + 4 + j*2);
        m->flags.timers = 1;
        debugmsg68(
                "file68: sndh -- FLAG #%02d -- %s\n", j+1, b+i+off);
        /* parse the flad */
        for (k=i+off; k<len && b[k]; ++k)
          switch (b[k]) {
          case 'y': m->flags.ym     = 1; break;
          case 'e': m->flags.ste    = 1; break;
          case 'a': m->flags.timera = 1; break;
          case 'b': m->flags.timerb = 1; break;
          case 'c': m->flags.timerc = 1; break;
          case 'd': m->flags.timerd = 1; break;
          case 'p': m->flags.amiga  = 1; break;
          }
        if (k > max)
          max = k;
      }
      i = max+1;

    } else if ( !memcmp(b+i,"##",2) && ( (ctypes & 0xC00) == 0xC00 ) ) {
      mb->nb_six = ( b[i+2] - '0' ) * 10 + ( b[i+3] - '0' );
      /* assert(0); */
      t = i; i += 4;
    } else if (!memcmp(b+i,"!#SN",4)) {
      /* track names */
      int j, max=0, tracks = mb->nb_six <= 0 ? 1 : mb->nb_six;
      t = i;
      /* assert(0); */
      for (j = 0; j < tracks; ++j) {
        int off = WPeekBE(b + i + 4 + j*2);
        if (off > max) max = off;
        mb->mus[j].tags.tag.title.val = b + i + off;
        debugmsg68(
                "file68: sndh -- !#SN #%02d pos:%d -- '%s'\n",
                j+1, i+off, mb->mus[j].tags.tag.title.val);
      }
      /* Position on the last sub name and skip it. */
      for (i += max; i < len && b[i] ; i++ )
        ;
      for (; i < len && !b[i] ; i++ )
        ;
    } else if ( !memcmp(b+i,"!#",2) && ( (ctypes & 0xC00) == 0xC00 ) ) {
      mb->default_six = ( b[i+2] - '0' ) * 10 + ( b[i+3] - '0' ) - 1;
      t = i; i += 4;
    } else if ( !memcmp(b+i,"!V",2) && ( (ctypes & 0xC00) == 0xC00 ) ) {
      vbl = ( b[i+2] - '0' ) * 10 + ( b[i+3] - '0' );
      i += 4;
    } else if (!memcmp(b+i,"**",2)) {
      /* FX + string 2 char ??? */
      debugmsg68("file68: sndh -- what to do with tag ? -- '**%c%c'\n",
                    b[i+2], b[i+3]);
      i += 4;
    } else if ( b[i] == 'T' && b[i+1] >= 'A' && b[i+1] <= 'D') {
      t = i; s = i += 2;
      myatoi(b, i, len, &frq);
    } else if( memcmp( b + i, empty_tag, 4 ) == 0 ||
               memcmp( b + i, "HDNS", 4 ) == 0 ) {
      t = i;
      i = len;
    } else {
      /* skip until next 0 byte, as long as it's inside the tag area */
      i += 4;
      while( *(b + i) != 0 && i < len ) {
        i++;
      }
    }

    if ( t < 0 ) {
      /* Unkwown tag, finish here. */
      ++unknowns;
      debugmsg68(
              "file68: sndh -- not a tag at offset %d -- %02X%02X%02X%02X\n",
              i, b[i]&255, b[i+1]&255, b[i+2]&255, b[i+3]&255);
      ++i;

      if (fail || unknowns >= unknowns_max) {
        i = len;
      }

    } else {
      /* Well known tag */

      debugmsg68(
              "file68: sndh -- got TAG -- '%c%c%c%c'\n",
              b[t], b[t+1], b[t+2], b[t+3]);
      unknowns = 0; /* Reset successive unkwown. */

      if (s >= 0) {
        int j, k;
        for ( j = s, k = s - 1; j < len && b[j]; ++j) {
          if ( b[j] < 32 ) b[j] = 32;
          else k = j;                   /* k is last non space char */
        }

        if (k+1 < len) {
          b[k+1] = 0;                   /* Strip triling space */
          i = k+2;
          if (p)
            *p = b+s;                     /* store tag */
          else
            debugmsg68("file68: sndh -- not storing -- '%s'\n",
                    b+s);

          /* HAXXX: using name can help determine STE needs */
          /* if (p == &mb->tags.tag.title.val) */
          /*   steonly = 0 */
          /*     || !!strstr(mb->tags.tag.title.val,"STE only") */
          /*     || !!strstr(mb->tags.tag.title.val,"(STe)") */
          /*     || !!strstr(mb->tags.tag.title.val,"(STE)") */
          /*     ; */

          debugmsg68(
                  "file68: sndh -- got ARG -- '%s'\n",
                  b+s);

        }

        /* skip the trailing null chars */
        for ( ; i < len && !b[i] ; i++ )
          ;
      }

    }
  }

  if (mb->nb_six <= 0) {
    debugmsg68(
            "file68: sndh -- %d track; assuming 1 track\n", mb->nb_six);
    mb->nb_six = 1;
  }

  if (mb->nb_six > SC68_MAX_TRACK) {
    mb->nb_six = SC68_MAX_TRACK;
  }

  for (i=0; i<mb->nb_six; ++i) {
    mb->mus[i].d0    = i+1;
//    mb->mus[i].loops = 0;		// FIXME
    mb->mus[i].frq   = frq ? frq : vbl;	
    if (!mb->mus[i].flags.timers) {
      /* Did not have the 'FLAG' tag, fallback to YM+STE */
      mb->mus[i].flags.ym  = 1;
      mb->mus[i].flags.ste = 1;
    }
  }
	// fallback for SNDH files
	if (mb->mus[0].name == 0) {
		mb->mus[0].name= mb->tags.tag.title.val;	
	}
	if (mb->mus[0].author == 0) {
		mb->mus[0].author= mb->mus[0].tags.tag.artist.val;
	}
  return 0;
}

/* Load , allocate memory and valid struct for SC68 music
 */
disk68_t * SC68file_load(istream_t * is)
{
  disk68_t *mb = 0;
  int len, room;
  int chk_size;
  music68_t *cursix;
  char *b;
  const char *fname = istream_filename(is);
  const char *errorstr = "no more info";

  if (!fname) {
    fname = "<nil>";
  }

  /* Open stream. */
  if (istream_open(is) == -1) {
    errorstr = "open failure";
    goto error;
  }


  /* Read header and get data length. */
  if (len = read_header(is), len < 0) {

    /* Verify tells it is a gzip or unice file, so we may give it a try.
     * $$$ Use filename to test if not a memory stream. It is not very
     * accurate since we can have other streams than memory ones that are
     * not files.
     */
    if (1) {
      void * buffer = 0;
      int l;
      switch (len) {
      case -'gzip':
	/* gzipped */
	if (memcmp(fname, "mem://",6)) {
	  buffer = gzip_load(fname, &l);
	}
	break;
      case -'ice!':
	debugmsg68("ICE LOAD [%s]!\n", fname);
	if (istream_seek_to(is,0) == 0) {
	  buffer = ice_load(is, &l);
	  debugmsg68("-> %p %d\n", buffer, l);
	}
	break;
      case -'sndh':
	debugmsg68("SNDH dans la place\n");
	if (istream_seek_to(is,0) != 0) {
	  break;
	}
	len = istream_length(is);
	if (len <= 32) {
	  break;
	}
	mb = SC68alloc(sizeof(*mb) + len);
	if (!mb) {
	  break;
	}
	memset(mb,0,sizeof(*mb));
	if (istream_read(is, mb->data, len) != len) {
	  break;
	}
	if (sndh_info(mb, len)) {
	  break;
	}
	goto validate;
      }

      if (buffer) {
	mb = SC68file_load_mem(buffer, l);
	SC68free(buffer);
	if (mb) {
	  return mb;
	}
      }
    }
    errorstr = "read header";
    goto error;
  }

  room = len + sizeof(disk68_t) - sizeof(mb->data);
  mb = SC68alloc(room);
  if (!mb) {
    errorstr = "memory allocation";
    goto error;
  }
  memset(mb, 0, room);

  if (istream_read(is, mb->data, len) != len) {
    errorstr = "read data";
    goto error;
  }

  for (b = mb->data, cursix = 0; len >= 8; b += chk_size, len -= chk_size) {
    char chk[8];

    if (b[0] != 'S' || b[1] != 'C') {
      break;
    }

    chk[0] = b[2];
    chk[1] = b[3];
    chk[2] = 0;
    chk_size = LPeek(b + 4);
    b += 8;
    len -= 8;

    if (!strcmp(chk, CH68_BASE)) {
      /* nothing to do. */
    }
    /* Music general info */
    else if (!strcmp(chk, CH68_DEFAULT)) {
      mb->default_six = LPeek(b);
    } else if (!strcmp(chk, CH68_FNAME)) {
      mb->name = b;
    }
    /* start music session */
    else if (!strcmp(chk, CH68_MUSIC)) {
      /* More than 256 musix !!! : Prematured end */
      if (mb->nb_six >= 256) {
	len = 0;
	break;
      }
      cursix = mb->mus + mb->nb_six;
      mb->nb_six++;
    }
    /* Music name */
    else if (!strcmp(chk, CH68_MNAME)) {
      if (cursix == 0) {
	errorstr = chk;
	goto error;
      }
      cursix->name = b;
    }
    /* Author name */
    else if (!strcmp(chk, CH68_ANAME)) {
      if (cursix == 0) {
	errorstr = chk;
	goto error;
      }
      cursix->author = b;
    }
    /* Composer name */
    else if (!strcmp(chk, CH68_CNAME)) {
      if (cursix == 0) {
	errorstr = chk;
	goto error;
      }
      cursix->composer = b;
    }
    /* External replay */
    else if (!strcmp(chk, CH68_REPLAY)) {
      if (cursix == 0) {
	errorstr = chk;
	goto error;
      }
      cursix->replay = b;
    }
    /* 68000 D0 init value */
    else if (!strcmp(chk, CH68_D0)) {
      if (cursix == 0) {
	errorstr = chk;
	goto error;
      }
      cursix->d0 = LPeek(b);
    }
    /* 68000 memory load address */
    else if (!strcmp(chk, CH68_AT)) {
      if (cursix == 0) {
	errorstr = chk;
	goto error;
      }
      cursix->a0 = LPeek(b);
    }
    /* Playing time */
    else if (!strcmp(chk, CH68_TIME)) {
      if (cursix == 0) {
	errorstr = chk;
	goto error;
      }
      cursix->time_ms = LPeek(b) * 1000u;
    }

    /* Playing time */
    else if (!strcmp(chk, CH68_FRAME)) {
      if (cursix == 0) {
	errorstr = chk;
	goto error;
      }
      cursix->frames = LPeek(b);
    }

    /* Replay frequency */
    else if (!strcmp(chk, CH68_FRQ)) {
      if (cursix == 0) {
	errorstr = chk;
	goto error;
      }
      cursix->frq = LPeek(b);
    }
    /* replay flags */
    else if (!strcmp(chk, CH68_TYP)) {
      int f;
      if (cursix == 0) {
	errorstr = chk;
	goto error;
      }
//       *(int *) &cursix->flags = LPeek(b);	FIXME original
	  
	  // FIXME merged from 3.x
      f = LPeek(b);
//      cursix->flags.all = 0;		// FIXME
      cursix->flags.ym        = !! (f & SC68_YM);
      cursix->flags.ste       = !! (f & SC68_STE);
      cursix->flags.amiga     = !! (f & SC68_AMIGA);
      cursix->flags.stechoice = !! (f & SC68_STECHOICE);
      cursix->flags.timers    = !! (f & SC68_TIMERS);
      cursix->flags.timera    = !! (f & SC68_TIMERA);
      cursix->flags.timerb    = !! (f & SC68_TIMERB);
      cursix->flags.timerc    = !! (f & SC68_TIMERC);
      cursix->flags.timerd    = !! (f & SC68_TIMERD);
    }
    /* music data */
    else if (!strcmp(chk, CH68_MDATA)) {
      if (cursix == 0) {
	errorstr = chk;
	goto error;
      }
      cursix->data = b;
      cursix->datasz = chk_size;
    }
    /* EOF */
    else if (!strcmp(chk, CH68_EOF)) {
      len = 0;
      break;
    }
  }

  /* Check it */
  if (len) {
    errorstr = "prematured end of file";
    goto error;
  }
 validate:
  if (valid(mb)) {
    errorstr = "validation test";
    goto error;
  }

  return mb;

error:
  istream_close(is);
  SC68free(mb);
  SC68error_add("SC68file_load(%s) : Failed (%s)", fname, errorstr);
  return 0;
}

#ifndef _FILE68_NO_SAVE_FUNCTION_

/* save CHUNK and data */
static int save_chunk(istream_t * os,
		      const char * chunk, const void * data, int size)
{
  chunk68_t chk;

  memcpy(chk.id, CH68_CHUNK, 2);
  memcpy(chk.id + 2, chunk, 2);
  LPoke(chk.size, size);
  if (istream_write(os, &chk, (int) sizeof(chunk68_t)) != sizeof(chunk68_t)) {
    goto error;
  }
  if (size > 0 && data && istream_write(os, data, size) != size) {
    goto error;
  }
  return 0;

 error:
  return -1;
}

/* save CHUNK and string (only if non-0 & lenght>0) */
static int save_string(istream_t * os,
		       const char * chunk, const char * str)
{
  int len;

  if (!str) {
    return 0;
  }
  if(len = strlen(str), !len) {
    return 0;
  }
  return save_chunk(os, chunk, str, len + 1);
}

/* save CHUNK & string str ( only if oldstr!=str & lenght>0 ) */
static int save_differstr(istream_t * os,
			  const char *chunk, char *str, char *oldstr)
{
  int len;

  if (oldstr == str
      || !str
      || (oldstr && !strcmp(oldstr, str))) {
    return 0;
  }
  len = strlen(str);
  return !len ? 0 :save_chunk(os, chunk, str, len + 1);
}

/* save CHUNK and 4 bytes Big Endian integer */
static int save_number(istream_t * os, const char * chunk, int n)
{
  char number[4];

  LPoke(number, n);
  return save_chunk(os, chunk, number, 4);
}

/* save CHUNK and number (only if n!=0) */
static int save_nonzero(istream_t * os, const char * chunk, int n)
{
  return !n ? 0 : save_number(os, chunk, n);
}

int SC68file_save_file(const char * fname, const disk68_t * mb)
{
  istream_t * os;
  int err;

  os = istream_file_create(fname,2);
  err = SC68file_save(os, mb);
  istream_destroy(os);

  return err;
}

int SC68file_save_mem(const char * buffer, int len, const disk68_t * mb)
{
  istream_t * os;
  int err;

  os = istream_mem_create((char *)buffer,len,2);
  err = SC68file_save(os, mb);
  istream_destroy(os);

  return err;
}


/* Save disk into file. */
int SC68file_save(istream_t * os, const disk68_t * mb)
{
  const music68_t * mus;
  int pos, len;
  const char * fname = "<nil>", * errstr = "success";
  char * mname, * aname, * cname, * data;

  /* Check parameters. */
  if (!os || !mb) {
    return SC68error_add("SC68file_save(%p,%p) NULL pointer(s)",
			 os, mb);
  }

  /* Get filename (for error message) */
  fname = istream_filename(os);

  /* Check number of music */
  if (mb->nb_six <= 0 || mb->nb_six > SC68_MAX_TRACK) {
    errstr = "bad number of track";
    goto error;
  }

  /* Open stream */
  if (istream_open(os) == -1) {
    errstr = "open error";
    goto error;
  }

  /* SC68 file header string */
  if (istream_write(os, SC68file_idstr, (int) sizeof(SC68file_idstr)) == -1) {
    errstr = "header write error";
    goto error;
  }

  /* Save pos for further calculation */
  if (pos = istream_tell(os), pos == -1) {
    errstr = "tell error";
    goto error;
  }

  errstr = "chunk write error";

  /* SC68 disk-info chunks */
  if (save_chunk(os, CH68_BASE, 0, 0)
      || save_string(os, CH68_FNAME, mb->name)
      || save_nonzero(os, CH68_DEFAULT, mb->default_six)
      ) {
    goto error;
  }

  /* Reset previous value for various string */
  mname = mb->name;
  aname = cname = data = 0;
  for (mus = mb->mus; mus < mb->mus + mb->nb_six; mus++) {
    /* Save track-name, author, composer, replay */
    if (save_chunk(os, CH68_MUSIC, 0, 0) == -1
	|| save_differstr(os, CH68_MNAME, mus->name, mname)
	|| save_differstr(os, CH68_ANAME, mus->author, aname)
	|| save_differstr(os, CH68_CNAME, mus->composer, cname)
	|| save_string(os, CH68_REPLAY, mus->replay)
	) {
      goto error;
    }
    if (mus->name) {
      mname = mus->name;
    }
    if (mus->author) {
      aname = mus->author;
    }
    if (mus->composer) {
      cname = mus->composer;
    }

    /* Save play parms */
    if (save_nonzero(os, CH68_D0, mus->d0)
	|| save_nonzero(os, CH68_AT, (mus->a0 == SC68_LOADADDR) ? 0 : mus->a0)
	|| save_nonzero(os, CH68_FRAME, mus->frames)
	|| save_nonzero(os, CH68_TIME, mus->time_ms / 1000u)
	|| save_nonzero(os, CH68_FRQ, (mus->frq == 50) ? 0 : mus->frq)
	|| save_number(os, CH68_TYP, *(int *) &mus->flags)
	) {
      goto error;
    }

    /* Save music data */
    if (mus->data && mus->data != data) {
      if (save_chunk(os, CH68_MDATA, mus->data, mus->datasz)) {
	goto error;
      }
      data = mus->data;
    }
  }

  /* SC68 last chunk */
  if (save_chunk(os, CH68_EOF, 0, 0)) {
    goto error;
  }
  
  /* Write total length */
  if(len = istream_tell(os), len == -1) {
    errstr = "tell error";
    goto error;
  }
  if(istream_seek(os, pos - len) == -1) {
    errstr = "seek error";
    goto error;
  }

  if (save_chunk(os, CH68_BASE, 0, (int) len - pos)) {
    errstr = "chunk write error";
    goto error;
  }
      
  istream_close(os);
  return 0;

error:
  istream_close(os);
  return SC68error_add("SC68file_save(%s) : Failed (%s)", fname, errstr);
}

#endif /* #ifndef _FILE68_NO_SAVE_FUNCTION_ */

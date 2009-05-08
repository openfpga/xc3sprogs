/* Jedec .jed file parser

Copyright (C) Uwe Bonnes 2009

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */
/*
 * Using a slightly corrected version from IPAL libjedec
 * Copyright (c) 2000 Stephen Williams (steve@icarus.com)
 */ 

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

#include "jedecfile.h"
#include "io_exception.h"

static unsigned char*allocate_fusemap(unsigned size)
{
  unsigned char*ptr = (unsigned char*) calloc((size+7) / 8, 1);
      return ptr;
}

int jedec_get_fuse(jedec_data_t jed, unsigned idx)
{
      unsigned byte, bit;
      if(idx >= jed->fuse_count)
	throw  io_exception(std::string("jedec_get_fuse"));

      byte = idx / 8;
      bit  = idx % 8;
      return (jed->fuse_list[byte] & (1 << bit))? 1 : 0;
}

void jedec_set_fuse(jedec_data_t jed, unsigned idx, int blow)
{
      unsigned byte, bit;
      if(idx >= jed->fuse_count)
	throw  io_exception(std::string("jedec_set_fuse"));

      byte = idx / 8;
      bit  = idx % 8;

      if (blow)
            jed->fuse_list[byte] |=  (1 << bit);
      else
            jed->fuse_list[byte] &= ~(1 << bit);
}

struct state_mach {
      jedec_data_t jed;

      void (*state)(int ch, struct state_mach*m);

      unsigned int checksum;
      union {
            struct {
                  unsigned cur_fuse;
            } l;
      } m;
};

static void m_startup(int ch, struct state_mach*m);
static void m_base(int ch, struct state_mach*m);
static void m_C(int ch, struct state_mach*m);
static void m_L(int ch, struct state_mach*m);
static void m_Lfuse(int ch, struct state_mach*m);
static void m_Q(int ch, struct state_mach*m);
static void m_QF(int ch, struct state_mach*m);
static void m_QP(int ch, struct state_mach*m);
static void m_skip(int ch, struct state_mach*m);
static void m_N(int ch, struct state_mach*m);

int m_N_item;
int m_N_pos;
char m_N_strings[8][256];

static void m_startup(int ch, struct state_mach*m)
{
      switch (ch) {
          case '\002':
            m->state = m_base;
            break;

          default:
            break;
      }
}

static void m_base(int ch, struct state_mach*m)
{
      if (isspace(ch))
            return;

      switch (ch) {
          case 'L':
            m->state = m_L;
            m->m.l.cur_fuse = 0;
            break;
          case 'Q':
            m->state = m_Q;
            break;
          case 'C':
            m->state = m_C;
            m->jed->checksum = 0;
            break;
          case 'N':
            m->state = m_N;
	    m_N_item = -1;
            break;
          default:
            m->state = m_skip;
            break;
      }
}

static void m_C(int ch, struct state_mach*m)
{
      if (isspace(ch))
            return;

      if (ch == '*') {
            m->state = m_base;
            return;
      }

      if(ch >='0' && ch <='9')
        {
          m->jed->checksum <<=4;
          m->jed->checksum += ch - '0';
          return;
        }
      ch &= 0x5f;
      if(ch >='A' && ch <= 'F')
        {
          m->jed->checksum <<=4;
          m->jed->checksum += ch - '7';
          return;
        }

      printf("m_C: Dangling '%c' 0x%02x\n", ch, ch);fflush(stdout);
      throw  io_exception(std::string("m_C"));
 }  
static void m_L(int ch, struct state_mach*m)
{
      if (isdigit(ch)) {
            m->m.l.cur_fuse *= 10;
            m->m.l.cur_fuse += ch - '0';
            return;
      }

      if (isspace(ch)) {
            m->state = m_Lfuse;
            return;
      }

      if (ch == '*') {
            m->state = m_base;
            return;
      }

      printf("m_L: Dangling '%c' 0x%02x\n", ch, ch);fflush(stdout);
      m->state = 0;
}

static void m_Lfuse(int ch, struct state_mach*m)
{
      switch (ch) {

          case '*':
            m->state = m_base;
            break;

          case '0':
            jedec_set_fuse(m->jed, m->m.l.cur_fuse, 0);
            m->m.l.cur_fuse += 1;
            break;

          case '1':
            jedec_set_fuse(m->jed, m->m.l.cur_fuse, 1);
            m->m.l.cur_fuse += 1;
            break;

          case ' ':
          case '\n':
          case '\r':
            break;

          default:
	    printf("m_LFuse: Dangling '%c' 0x%02x\n", ch, ch);fflush(stdout);
            m->state = 0;
            break;
      }
}

#ifdef __unix__
#define stricmp strcasecmp
#define strnicmp strncasecmp
#endif

static void m_N(int ch, struct state_mach*m)
{
      switch (ch) {

      case '*':
	if ((stricmp(m_N_strings[0], "DEVICE")) == 0)
	  {
	    m_N_strings[m_N_item][m_N_pos] = 0;
	    strcpy(m->jed->device, m_N_strings[1]);
	  }
	m->state = m_base;
	m_N_item= -1;
	break;
      case ' ':
	if(m_N_item >=0)
	  m_N_strings[m_N_item][m_N_pos] = 0;
	m_N_item++;
	m_N_pos = 0;
      case '\n':
      case '\r':
	  break;
	  
      default:
	if((m_N_item >=0) && (m_N_item <8) && (m_N_pos <255))
	  m_N_strings[m_N_item][m_N_pos] = ch;
	m_N_pos++;
	break;
      }
}

static void m_Q(int ch, struct state_mach*m)
{
      switch (ch) {
          case 'F':
            if (m->jed->fuse_count != 0) {
                  m->state = 0;
                  return;
            }
            m->state = m_QF;
            m->jed->fuse_count = 0;
            break;
          case 'P':
            if (m->jed->pin_count != 0) {
                  m->state = 0;
                  return;
            }
            m->state = m_QP;
            m->jed->pin_count = 0;
            break;
          default:
            m->state = m_skip;
            break;
      }
}

static void m_QF(int ch, struct state_mach*m)
{
      if (isspace(ch))
            return;

      if (isdigit(ch)) {
            m->jed->fuse_count *= 10;
            m->jed->fuse_count += ch - '0';
            return;
      }

      if (ch == '*') {
            m->state = m_base;
            m->jed->fuse_list = allocate_fusemap(m->jed->fuse_count);
            return;
      }

      throw  io_exception(std::string("m_QF"));
}

static void m_QP(int ch, struct state_mach*m)
{
      if (isspace(ch))
            return;

      if (isdigit(ch)) {
            m->jed->pin_count *= 10;
            m->jed->pin_count += ch - '0';
            return;
      }

      if (ch == '*') {
            m->state = m_base;
            return;
      }

      throw  io_exception(std::string("m_QP"));
}

static void m_skip(int ch, struct state_mach*m)
{
      switch (ch) {
          case '*':
            m->state = m_base;
            break;

          default:
            break;
      }
}
JedecFile::JedecFile(void)
  : Error(false), logfile(stderr) 
{
  jed.fuse_count = 0;
  jed.pin_count = 0;
  jed.fuse_list = 0;
  jed.device[0] = 0;
}

JedecFile::~JedecFile(void)
{
  if(jed.fuse_list)
    free(jed.fuse_list);
}

void JedecFile::readFile(char const * fname)
{
  int ch;
  struct state_mach m;

  FILE *const  fp=fopen(fname,"rb");
  if(!fp)  throw  io_exception(std::string("Cannot open file ") + fname);
  
  //jed = (jedec_data_t)calloc(1, sizeof(struct jedec_data));
  m.jed = &jed;
  m.state = m_startup;
  while ((ch = fgetc(fp)) != EOF) {
    m.state(ch, &m);
    if (m.state == 0) {
      /* Some sort of error happened. */
      throw  io_exception(std::string("Unknown"));
    }
  }
}

void JedecFile::saveAsJed(const char  *device, const char  *fname)
{
  unsigned int i, b=0, l=0 ,w=0;
  unsigned short chksum=0;
  FILE *fp=fopen(fname,"wb");
  int DRegLength;
  int type=-1;

  if(!fp)
    {
      printf("Unable to open %s: %s\n", fname, strerror(errno));
      fclose(fp);
      return;
    }
  if (strnicmp("XC9536X",device, sizeof("XC9536X")-1) == 0)
    {
      type = JED_XC95X;
      DRegLength=2;
    }
  if (strnicmp("XC9572X",device, sizeof("XC9572X")-1) == 0)
    {
      type = JED_XC95X;
      DRegLength=4;
    }
  if (strnicmp("XC95144X",device, sizeof("XC95144X")-1) == 0)
    {
      type = JED_XC95X;
    DRegLength=8;
    }
  if (strnicmp("XC95288X",device, sizeof("XC95288X")-1) == 0)
    {
      type = JED_XC95X;
      DRegLength=16;
    } 

  if(type == JED_XC95X)
    {
      /* Xilinx Impact (10.1) needs following additional items 
	 to recognizes as a valid Jedec File
       * the 4 Digits as total Checksum
       * N DEVICE
       */
 
      fprintf(fp, "\2QF%d*\nQV0*\nF0*\nX0*\nJ0 0*\n",jed.fuse_count);
      fprintf(fp, "N DEVICE %s*\n", device);

      for (i=0; i<jed.fuse_count; i++)
	{
	  if(!w && !b)
	    fprintf(fp, "L%07d",i);
	  if (!b)
	    fprintf(fp, " ");
	  fprintf(fp, "%c",( jed.fuse_list[i/8] & (1 << (i%8)))?'1':'0');
	  if (l<9)
	    {
	      if(b==7)
		b=0;
	      else
		b++;
	    }
	  else 
	    {
	      if (b == 5)
		b = 0;
	      else
		b++;
	    }
	  if(!b)
	    {
	      if (w == (DRegLength-1))
		{
		  fprintf(fp, "*\n");
		  w = 0;
		  l++;
		}
	      else
		w++;
	    }
	  if (l==15)
	    l =0;
	}
    }

  for(i=0; i<jed.fuse_count/8; i++)
    chksum += jed.fuse_list[i];
  fprintf(fp, "C%04X*\n%c0000\n", chksum, 3);
  fclose(fp);
  
}

void JedecFile::setLength(unsigned int f_count)
{
  jed.fuse_count = f_count;
  jed.fuse_list = new byte[f_count+7/8];
}

void JedecFile::set_fuse(unsigned int idx, int blow)
{
  jedec_set_fuse(&jed, idx,blow);
}

int JedecFile::get_fuse(unsigned int idx)
{
  return jedec_get_fuse(&jed, idx);
}

unsigned short JedecFile::calcChecksum()
{
  int i;
  unsigned short cc=0;
  
  for(i=0; i<jed.fuse_count/8; i++)
    cc += jed.fuse_list[i];
  return cc;
}

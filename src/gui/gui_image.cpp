// GoatTracker GUI: images built into the program. A small PNG decoder (8-bit
// RGB or RGBA, not interlaced: what the built-in images are) and a resampler
// that makes a copy of exactly the size shown, so that the image is drawn
// pixel for pixel.

#include "gui_internal.h"

/*
 * Inflate (RFC 1950 / 1951): stored, fixed and dynamic Huffman blocks
 */

namespace
{
  struct Huffman
  {
    short count[16];      // Codes of each length
    short symbol[320];    // Symbols in canonical order
  };

  struct Inflater
  {
    const unsigned char *in;
    size_t inlen, inpos;
    unsigned bitbuf;
    int bitcount;
    unsigned char *out;
    size_t outlen, outpos;
    bool error;
  };

  int getbits(Inflater *s, int n)
  {
    unsigned v = s->bitbuf;
    while (s->bitcount < n)
    {
      if (s->inpos >= s->inlen)
      {
        s->error = true;
        return 0;
      }
      v |= (unsigned)s->in[s->inpos++] << s->bitcount;
      s->bitcount += 8;
    }
    s->bitbuf = v >> n;
    s->bitcount -= n;
    return (int)(v & ((1u << n) - 1));
  }

  // Builds the decoding table from the code lengths. Returns false if they
  // are not a valid code
  bool makehuffman(Huffman *h, const unsigned char *lengths, int n)
  {
    short offs[16];
    int len, sym, left = 1;

    for (len = 0; len < 16; len++) h->count[len] = 0;
    for (sym = 0; sym < n; sym++) h->count[lengths[sym]]++;
    if (h->count[0] == n) return true;          // No codes: nothing to decode
    for (len = 1; len < 16; len++)
    {
      left <<= 1;
      left -= h->count[len];
      if (left < 0) return false;               // Too many codes
    }
    offs[1] = 0;
    for (len = 1; len < 15; len++) offs[len + 1] = offs[len] + h->count[len];
    for (sym = 0; sym < n; sym++)
    {
      if (lengths[sym]) h->symbol[offs[lengths[sym]]++] = (short)sym;
    }
    return true;
  }

  int decodesym(Inflater *s, const Huffman *h)
  {
    int code = 0, first = 0, index = 0, len;

    for (len = 1; len < 16; len++)
    {
      int count;
      code |= getbits(s, 1);
      if (s->error) return -1;
      count = h->count[len];
      if (code - count < first) return h->symbol[index + (code - first)];
      index += count;
      first += count;
      first <<= 1;
      code <<= 1;
    }
    s->error = true;
    return -1;
  }

  const short lengthbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
  const short lengthextra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
  const short distbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
    4097, 6145, 8193, 12289, 16385, 24577 };
  const short distextra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

  bool inflatecodes(Inflater *s, const Huffman *lencode, const Huffman *distcode)
  {
    for (;;)
    {
      int sym = decodesym(s, lencode);
      if (s->error) return false;
      if (sym < 256)
      {
        if (s->outpos >= s->outlen) return false;
        s->out[s->outpos++] = (unsigned char)sym;
      }
      else if (sym == 256) return true;
      else
      {
        size_t len, dist;
        sym -= 257;
        if (sym >= 29) return false;
        len = lengthbase[sym] + getbits(s, lengthextra[sym]);
        sym = decodesym(s, distcode);
        if ((s->error) || (sym < 0) || (sym >= 30)) return false;
        dist = distbase[sym] + getbits(s, distextra[sym]);
        if ((s->error) || (dist > s->outpos) || (len > s->outlen - s->outpos)) return false;
        while (len--)
        {
          s->out[s->outpos] = s->out[s->outpos - dist];
          s->outpos++;
        }
      }
    }
  }

  bool inflatestored(Inflater *s)
  {
    unsigned len, nlen;

    s->bitbuf = 0;
    s->bitcount = 0;
    if (s->inpos + 4 > s->inlen) return false;
    len = s->in[s->inpos] | (s->in[s->inpos + 1] << 8);
    nlen = s->in[s->inpos + 2] | (s->in[s->inpos + 3] << 8);
    s->inpos += 4;
    if ((len != (~nlen & 0xffff)) || (s->inpos + len > s->inlen) || (len > s->outlen - s->outpos)) return false;
    memcpy(s->out + s->outpos, s->in + s->inpos, len);
    s->inpos += len;
    s->outpos += len;
    return true;
  }

  bool inflatefixed(Inflater *s)
  {
    static Huffman lencode, distcode;
    static bool made = false;

    if (!made)
    {
      unsigned char lengths[288];
      int sym;
      for (sym = 0; sym < 144; sym++) lengths[sym] = 8;
      for (; sym < 256; sym++) lengths[sym] = 9;
      for (; sym < 280; sym++) lengths[sym] = 7;
      for (; sym < 288; sym++) lengths[sym] = 8;
      makehuffman(&lencode, lengths, 288);
      for (sym = 0; sym < 30; sym++) lengths[sym] = 5;
      makehuffman(&distcode, lengths, 30);
      made = true;
    }
    return inflatecodes(s, &lencode, &distcode);
  }

  bool inflatedynamic(Inflater *s)
  {
    static const unsigned char order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    unsigned char lengths[320];
    Huffman lencode, distcode;
    int nlen, ndist, ncode, index;

    nlen = getbits(s, 5) + 257;
    ndist = getbits(s, 5) + 1;
    ncode = getbits(s, 4) + 4;
    if ((s->error) || (nlen > 286) || (ndist > 30)) return false;
    for (index = 0; index < ncode; index++) lengths[order[index]] = (unsigned char)getbits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if ((s->error) || (!makehuffman(&lencode, lengths, 19))) return false;

    index = 0;
    while (index < nlen + ndist)
    {
      int sym = decodesym(s, &lencode);
      int len = 0, rep;
      if ((s->error) || (sym < 0)) return false;
      if (sym < 16)
      {
        lengths[index++] = (unsigned char)sym;
        continue;
      }
      if (sym == 16)
      {
        if (!index) return false;
        len = lengths[index - 1];
        rep = 3 + getbits(s, 2);
      }
      else if (sym == 17) rep = 3 + getbits(s, 3);
      else rep = 11 + getbits(s, 7);
      if ((s->error) || (index + rep > nlen + ndist)) return false;
      while (rep--) lengths[index++] = (unsigned char)len;
    }
    if (!lengths[256]) return false;          // No end of block code
    if ((!makehuffman(&lencode, lengths, nlen)) || (!makehuffman(&distcode, lengths + nlen, ndist))) return false;
    return inflatecodes(s, &lencode, &distcode);
  }

  // zlib data into out (outlen bytes expected). Returns the bytes made
  size_t zinflate(const unsigned char *in, size_t inlen, unsigned char *out, size_t outlen)
  {
    Inflater s;
    int last;

    if ((inlen < 2) || ((in[0] & 0x0f) != 8) || (((in[0] << 8) | in[1]) % 31) || (in[1] & 0x20)) return 0;
    memset(&s, 0, sizeof s);
    s.in = in + 2;
    s.inlen = inlen - 2;
    s.out = out;
    s.outlen = outlen;
    do
    {
      bool ok;
      last = getbits(&s, 1);
      switch (getbits(&s, 2))
      {
        case 0: ok = inflatestored(&s); break;
        case 1: ok = inflatefixed(&s); break;
        case 2: ok = inflatedynamic(&s); break;
        default: ok = false;
      }
      if ((!ok) || (s.error)) return 0;
    } while (!last);
    return s.outpos;
  }

  unsigned be32(const unsigned char *p)
  {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
  }
}

unsigned char *gui::decodepng(const unsigned char *data, size_t size, int *w, int *h)
{
  static const unsigned char signature[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
  unsigned width = 0, height = 0;
  int bpp = 0;
  size_t pos = 8, idatlen = 0, rawlen, stride, x, y;
  unsigned char *idat, *raw, *rgba;

  if ((size < 8) || (memcmp(data, signature, 8))) return NULL;

  // IHDR, and the size of all the IDAT chunks together
  while (pos + 12 <= size)
  {
    unsigned len = be32(data + pos);
    const unsigned char *type = data + pos + 4;
    const unsigned char *body = data + pos + 8;
    if (pos + 12 + len > size) return NULL;
    if (!memcmp(type, "IHDR", 4))
    {
      if (len < 13) return NULL;
      width = be32(body);
      height = be32(body + 4);
      // 8 bits, RGB or RGBA, no interlace
      if ((body[8] != 8) || ((body[9] != 2) && (body[9] != 6)) || (body[12] != 0)) return NULL;
      bpp = (body[9] == 6) ? 4 : 3;
    }
    if (!memcmp(type, "IDAT", 4)) idatlen += len;
    pos += 12 + len;
  }
  if ((!bpp) || (!width) || (!height) || (width > 16384) || (height > 16384) || (!idatlen)) return NULL;

  idat = (unsigned char *)malloc(idatlen);
  if (!idat) return NULL;
  for (pos = 8, idatlen = 0; pos + 12 <= size;)
  {
    unsigned len = be32(data + pos);
    if (!memcmp(data + pos + 4, "IDAT", 4))
    {
      memcpy(idat + idatlen, data + pos + 8, len);
      idatlen += len;
    }
    pos += 12 + len;
  }

  stride = (size_t)width * bpp;
  rawlen = (stride + 1) * height;
  raw = (unsigned char *)malloc(rawlen);
  rgba = (unsigned char *)malloc((size_t)width * height * 4);
  if ((!raw) || (!rgba) || (zinflate(idat, idatlen, raw, rawlen) != rawlen))
  {
    free(idat);
    free(raw);
    free(rgba);
    return NULL;
  }
  free(idat);

  // Undo the filters, row by row (in place), then to RGBA
  for (y = 0; y < height; y++)
  {
    unsigned char *line = raw + y * (stride + 1);
    unsigned char *cur = line + 1;
    const unsigned char *prev = y ? raw + (y - 1) * (stride + 1) + 1 : NULL;
    int filter = line[0];

    for (x = 0; x < stride; x++)
    {
      int a = (x >= (size_t)bpp) ? cur[x - bpp] : 0;
      int b = prev ? prev[x] : 0;
      int c = ((prev) && (x >= (size_t)bpp)) ? prev[x - bpp] : 0;
      int pred;
      switch (filter)
      {
        case 0: pred = 0; break;
        case 1: pred = a; break;
        case 2: pred = b; break;
        case 3: pred = (a + b) >> 1; break;
        case 4:
        {
          int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
          pred = ((pa <= pb) && (pa <= pc)) ? a : (pb <= pc) ? b : c;
          break;
        }
        default:
          free(raw);
          free(rgba);
          return NULL;
      }
      cur[x] = (unsigned char)(cur[x] + pred);
    }
    for (x = 0; x < width; x++)
    {
      unsigned char *d = rgba + (y * width + x) * 4;
      const unsigned char *p = cur + x * bpp;
      d[0] = p[0];
      d[1] = p[1];
      d[2] = p[2];
      d[3] = (bpp == 4) ? p[3] : 255;
    }
  }
  free(raw);
  *w = (int)width;
  *h = (int)height;
  return rgba;
}

/*
 * Resampling: each output pixel averages the source pixels it covers (when
 * smaller), or is interpolated between the nearest ones (when larger)
 */

namespace
{
  struct Taps
  {
    int *first;           // First source pixel of each output pixel
    int *count;
    float *weight;        // maxtaps weights for each output pixel
    int maxtaps;
  };

  bool maketaps(Taps *t, int srcn, int dstn)
  {
    double scale = (double)srcn / dstn;
    int i;

    t->maxtaps = (scale > 1.0) ? (int)ceil(scale) + 2 : 2;
    t->first = (int *)malloc(sizeof(int) * dstn);
    t->count = (int *)malloc(sizeof(int) * dstn);
    t->weight = (float *)calloc((size_t)dstn * t->maxtaps, sizeof(float));
    if ((!t->first) || (!t->count) || (!t->weight)) return false;
    for (i = 0; i < dstn; i++)
    {
      float *w = t->weight + (size_t)i * t->maxtaps;
      if (scale > 1.0)
      {
        // The source span [s0, s1) of this pixel, partial pixels at the ends
        double s0 = i * scale, s1 = (i + 1) * scale;
        int k0 = (int)floor(s0), k1 = (int)ceil(s1), k;
        if (k1 > srcn) k1 = srcn;
        t->first[i] = k0;
        t->count[i] = k1 - k0;
        for (k = k0; k < k1; k++)
        {
          double a = (k > s0) ? k : s0, b = (k + 1 < s1) ? k + 1 : s1;
          w[k - k0] = (float)((b - a) / scale);
        }
      }
      else
      {
        double c = (i + 0.5) * scale - 0.5;
        int k0 = (int)floor(c);
        float f = (float)(c - k0);
        if (k0 < 0)
        {
          k0 = 0;
          f = 0.0f;
        }
        if (k0 >= srcn - 1)
        {
          k0 = srcn - 1;
          f = 0.0f;
        }
        t->first[i] = k0;
        t->count[i] = (f > 0.0f) ? 2 : 1;
        w[0] = 1.0f - f;
        w[1] = f;
      }
    }
    return true;
  }

  void freetaps(Taps *t)
  {
    free(t->first);
    free(t->count);
    free(t->weight);
  }
}

SDL_Texture *gui::scaledtexture(SDL_Renderer *renderer, const unsigned char *rgba, int sw, int sh, int w, int h)
{
  Taps tx, ty;
  float *acc;
  Uint32 *pixels;
  SDL_Texture *texture = NULL;
  int x, y, k, j;

  if ((!rgba) || (w <= 0) || (h <= 0)) return NULL;
  memset(&tx, 0, sizeof tx);
  memset(&ty, 0, sizeof ty);
  acc = (float *)malloc(sizeof(float) * 3 * w);
  pixels = (Uint32 *)malloc(sizeof(Uint32) * w * h);
  if ((acc) && (pixels) && (maketaps(&tx, sw, w)) && (maketaps(&ty, sh, h)))
  {
    for (y = 0; y < h; y++)
    {
      const float *wy = ty.weight + (size_t)y * ty.maxtaps;
      memset(acc, 0, sizeof(float) * 3 * w);
      for (j = 0; j < ty.count[y]; j++)
      {
        // One source row across, premultiplied (that is, over black)
        const unsigned char *src = rgba + (size_t)(ty.first[y] + j) * sw * 4;
        for (x = 0; x < w; x++)
        {
          const float *wx = tx.weight + (size_t)x * tx.maxtaps;
          const unsigned char *p = src + (size_t)tx.first[x] * 4;
          float r = 0.0f, g = 0.0f, b = 0.0f;
          for (k = 0; k < tx.count[x]; k++, p += 4)
          {
            float a = p[3] * wx[k];
            r += p[0] * a;
            g += p[1] * a;
            b += p[2] * a;
          }
          acc[x * 3] += r * wy[j];
          acc[x * 3 + 1] += g * wy[j];
          acc[x * 3 + 2] += b * wy[j];
        }
      }
      for (x = 0; x < w; x++)
      {
        int c[3];
        for (k = 0; k < 3; k++)
        {
          c[k] = (int)(acc[x * 3 + k] / 255.0f + 0.5f);
          if (c[k] > 255) c[k] = 255;
          if (c[k] < 0) c[k] = 0;
        }
        pixels[(size_t)y * w + x] = 0xff000000u | ((Uint32)c[0] << 16) | ((Uint32)c[1] << 8) | (Uint32)c[2];
      }
    }
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (texture)
    {
      SDL_UpdateTexture(texture, NULL, pixels, w * 4);
      SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    }
  }
  freetaps(&tx);
  freetaps(&ty);
  free(acc);
  free(pixels);
  return texture;
}

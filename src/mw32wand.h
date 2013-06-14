/* Header file for ImageMagick image decoders.

This file is part of Meadow(Modified version of GNU Emacs).

GNU Emacs is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs; see the file COPYING.  If not, write to
the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* 2005/05/07 created by MIYOSHI Masanori (miyoshi@meadowy.org) */

#if !defined (_MW32WAND_H_)
#define _MW32WAND_H_

typedef struct _MagickWand MagickWand;
typedef struct _PixelWand PixelWand;
typedef struct _PixelIterator PixelIterator;

typedef enum
{
  MagickFalse = 0,
  MagickTrue = 1
} MagickBooleanType;

typedef enum
{
  UndefinedType,
  BilevelType,
  GrayscaleType,
  GrayscaleMatteType,
  PaletteType,
  PaletteMatteType,
  TrueColorType,
  TrueColorMatteType,
  ColorSeparationType,
  ColorSeparationMatteType,
  OptimizeType
} ImageType;

typedef enum
{
  UndefinedPixel,
  CharPixel,
  DoublePixel,
  FloatPixel,
  IntegerPixel,
  LongPixel,
  QuantumPixel,
  ShortPixel
} StorageType;

typedef unsigned int IndexPacket;

#define MaxTextExtent  ((size_t) 4096)

#endif /* _MW32WAND_H_ */

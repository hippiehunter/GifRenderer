#pragma once

#define GIFLIB_MAJOR 5
#define GIFLIB_MINOR 1
#define GIFLIB_RELEASE 0

#define GIF_ERROR   0
#define GIF_OK      1

#include <stddef.h>
#include <stdbool.h>
#include <stdexcept>
#include <array>
#include <vector>
#include <memory>

#define GIF_STAMP "GIFVER"          /* First chars in file - GIF stamp.  */
#define GIF_STAMP_LEN sizeof(GIF_STAMP) - 1
#define GIF_VERSION_POS 3           /* Version first character in stamp. */
#define GIF87_STAMP "GIF87a"        /* First chars in file - GIF stamp.  */
#define GIF89_STAMP "GIF89a"        /* First chars in file - GIF stamp.  */

typedef unsigned char GifPixelType;
typedef unsigned char *GifRowType;
typedef unsigned char GifByteType;
typedef unsigned int GifPrefixType;
typedef int GifWord;

struct GifColorType
{
  GifByteType Blue, Green, Red, Alpha;
};

template<typename N>
auto GIF_ASPECT_RATIO(N n) -> decltype(n + 15.0)
{
  return ((n)+15.0 / 64.0);
}

template<typename N>
uint16_t UNSIGNED_LITTLE_ENDIAN(N lo, N hi)
{
  return ((lo) | ((hi) << 8));
}

enum GifRecordType
{
  UNDEFINED_RECORD_TYPE,
  SCREEN_DESC_RECORD_TYPE,
  IMAGE_DESC_RECORD_TYPE, /* Begin with ',' */
  EXTENSION_RECORD_TYPE,  /* Begin with '!' */
  TERMINATE_RECORD_TYPE   /* Begin with ';' */
};

/******************************************************************************
GIF89 structures
******************************************************************************/
struct ExtensionBlock
{
  std::vector<GifByteType> Bytes; /* on malloc(3) heap */
  int Function;       /* The block function code */
#define CONTINUE_EXT_FUNC_CODE    0x00    /* continuation subblock */
#define COMMENT_EXT_FUNC_CODE     0xfe    /* comment */
#define GRAPHICS_EXT_FUNC_CODE    0xf9    /* graphics control (GIF89) */
#define PLAINTEXT_EXT_FUNC_CODE   0x01    /* plaintext */
#define APPLICATION_EXT_FUNC_CODE 0xff    /* application block */
};

#define NO_TRANSPARENT_COLOR	-1
struct GraphicsControlBlock
{
  GraphicsControlBlock(const ExtensionBlock& extension)
  {
    if (extension.Bytes.size() != 4)
    {
      throw std::runtime_error("invalid extension size");
    }

    DisposalMode = (extension.Bytes[0] >> 2) & 0x07;
    UserInputFlag = (extension.Bytes[0] & 0x02) != 0;
    DelayTime = UNSIGNED_LITTLE_ENDIAN(extension.Bytes[1], extension.Bytes[2]);
    if (extension.Bytes[0] & 0x01)
      TransparentColor = (int)extension.Bytes[3];
    else
      TransparentColor = NO_TRANSPARENT_COLOR;
  }
  int DisposalMode;
#define DISPOSAL_UNSPECIFIED      0       /* No disposal specified. */
#define DISPOSE_DO_NOT            1       /* Leave image in place */
#define DISPOSE_BACKGROUND        2       /* Set area too background color */
#define DISPOSE_PREVIOUS          3       /* Restore to previous content */
  bool UserInputFlag;      /* User confirmation required before disposal */
  int DelayTime;           /* pre-display delay in 0.01sec units */
  int TransparentColor;    /* Palette index for transparency, -1 if none */

};

struct ColorMapObject
{
private:
  /* return smallest bitfield size n will fit in */
  int GifBitSize(int n)
  {
    register int i;

    for (i = 1; i <= 8; i++)
      if ((1 << i) >= n)
        break;
    return (i);
  }
public:
  int BitsPerPixel;
  bool SortFlag;
  std::vector<GifColorType> Colors;

  void init(unsigned int colorCount)
  {
    if (colorCount != (unsigned int)(1 << GifBitSize(colorCount)))
    {
      colorCount++;
    }

    Colors.resize(colorCount);
    BitsPerPixel = GifBitSize(colorCount);
    SortFlag = false;
  }
};

struct GifImageDesc
{
  GifWord Left, Top, Width, Height;   /* Current image dimensions. */
  bool Interlace;                     /* Sequential/Interlaced lines. */
  ColorMapObject ColorMap;           /* The local color map */
};



class SavedImage
{
public:
  SavedImage() {}
  SavedImage(const SavedImage&) = delete;
  SavedImage(SavedImage&& mover)
  {
    ImageDesc = std::move(mover.ImageDesc);
    RasterBits = std::move(mover.RasterBits);
    ExtensionBlocks = std::move(mover.ExtensionBlocks);
  }
  GifImageDesc ImageDesc;
  std::unique_ptr<GifByteType[]> RasterBits;
  std::vector<ExtensionBlock> ExtensionBlocks;            /* Extensions before image */
};

#define EXTENSION_INTRODUCER      0x21
#define DESCRIPTOR_INTRODUCER     0x2c
#define TERMINATOR_INTRODUCER     0x3b

#define LZ_MAX_CODE         4095    /* Biggest code possible in 12 bits. */
#define LZ_BITS             12

#define FLUSH_OUTPUT        4096    /* Impossible code, to signal flush. */
#define FIRST_CODE          4097    /* Impossible code, to signal first. */
#define NO_SUCH_CODE        4098    /* Impossible code, to signal empty. */

template<typename USERDATA>
class GifDecompressor
{
private:
  GifWord BitsPerPixel;     /* Bits per pixel (Codes uses at least this + 1). */
  GifWord ClearCode;   /* The CLEAR LZ code. */
  GifWord EOFCode;     /* The EOF LZ code. */
  GifWord RunningCode; /* The next code algorithm can generate. */
  GifWord RunningBits; /* The number of bits required to represent RunningCode. */
  GifWord MaxCode1;    /* 1 bigger than max. possible code, in RunningBits bits. */
  GifWord LastCode;    /* The code before the current code. */
  GifWord CrntCode;    /* Current algorithm code. */
  GifWord StackPtr;    /* For character stack (see below). */
  GifWord CrntShiftState;    /* Number of bits in CrntShiftDWord. */
  unsigned long CrntShiftDWord;   /* For bytes decomposition into codes. */
  unsigned long PixelCount;   /* Number of pixels in image. */
  std::array<GifByteType, 256> Buf;   /* Compressed input is buffered here. */
  std::array<GifWord, LZ_MAX_CODE + 1> Stack; /* Decoded pixels are stacked here. */
  std::array<GifWord, LZ_MAX_CODE + 1> Suffix;    /* So we can trace the codes. */
  std::array<GifPrefixType, LZ_MAX_CODE + 1> Prefix;
public:
  GifDecompressor(USERDATA& userData, unsigned long pixelCount)
  {
    BitsPerPixel = 0;
    if (userData.read((GifByteType*)&BitsPerPixel, 1) != 1)
    {    /* Read Code size from file. */
      throw std::runtime_error("failed to initialize decompressor");
    }
    PixelCount = pixelCount;
    Buf[0] = 0;    /* Input Buffer empty. */
    ClearCode = (1 << BitsPerPixel);
    EOFCode = ClearCode + 1;
    RunningCode = EOFCode + 1;
    RunningBits = BitsPerPixel + 1;    /* Number of bits per code. */
    MaxCode1 = 1 << RunningBits;    /* Max. code + 1. */
    StackPtr = 0;    /* No pixels on the pixel stack. */
    LastCode = NO_SUCH_CODE;
    CrntShiftState = 0;    /* No information in CrntShiftDWord. */
    CrntShiftDWord = 0;
    for (int i = 0; i <= LZ_MAX_CODE; i++)
      Prefix[i] = NO_SUCH_CODE;
  }

  void GetCode(USERDATA& userData, int& CodeSize, GifByteType **CodeBlock)
  {
    CodeSize = BitsPerPixel;
    GetCodeNext(userData, CodeBlock);
  }

  bool GetCodeNext(USERDATA& userData, GifByteType **CodeBlock)
  {
    GifByteType buf;
    if (userData.read(&buf, 1) != 1)
    {
      throw std::runtime_error("failed to get coded pixel");
    }

    if (buf > 0)
    {
      *CodeBlock = &this->Buf[0];
      (*CodeBlock)[0] = buf;
      if (userData.read(&((*CodeBlock)[1]), buf) != buf)
      {
        throw std::runtime_error("failed to get coded pixels");
      }
      return true;
    }
    else
    {
      *CodeBlock = NULL;
      this->Buf[0] = 0;    /* Make sure the buffer is empty! */
      PixelCount = 0;    /* And local info. indicate image read. */
      return false;
    }

  }

  /******************************************************************************
  This routines read one GIF data block at a time and buffers it internally
  so that the decompression routine could access it.
  The routine returns the next byte from its internal buffer (or read next
  block in if buffer empty)
  ******************************************************************************/
  void BufferedInput(USERDATA& userData, GifByteType *buf, GifByteType *nextByte)
  {
    if (buf[0] == 0)
    {
      /* Needs to read the next buffer - this one is empty: */
      if (userData.read(buf, 1) != 1)
      {
        throw std::runtime_error("failed to read into buffer while decompressing");
      }
      /* There shouldn't be any empty data blocks here as the LZW spec
      * says the LZW termination code should come first.  Therefore we
      * shouldn't be inside this routine at that point.
      */
      if (buf[0] == 0)
      {
        throw std::runtime_error("invalid image expected termination code");
      }
      if (userData.read(&buf[1], buf[0]) != buf[0])
      {
        throw std::runtime_error("invalid image unexpected read failure");
      }
      *nextByte = buf[1];
      buf[1] = 2;    /* We use now the second place as last char read! */
      buf[0]--;
    }
    else
    {
      auto bufValue = buf[buf[1]];
      *nextByte = bufValue;
      buf[1] = ((buf[1] + 1) & 0xFF);
      buf[0]--;
    }
  }

  /******************************************************************************
  The LZ decompression input routine:
  This routine is responsable for the decompression of the bit stream from
  8 bits (bytes) packets, into the real codes.
  returns real code
  ******************************************************************************/
  int DecompressInput(USERDATA& userData)
  {
    static const unsigned short CodeMasks[] = {
      0x0000, 0x0001, 0x0003, 0x0007,
      0x000f, 0x001f, 0x003f, 0x007f,
      0x00ff, 0x01ff, 0x03ff, 0x07ff,
      0x0fff
    };
    int result = 0;
    GifByteType NextByte;

    /* The image can't contain more than LZ_BITS per code. */
    if (RunningBits > LZ_BITS)
    {
      throw std::runtime_error("bad image");
    }

    while (CrntShiftState < RunningBits)
    {
      /* Needs to get more bytes from input stream for next code: */
      BufferedInput(userData, &Buf[0], &NextByte);
      CrntShiftDWord |=
        ((unsigned long)NextByte) << CrntShiftState;
      CrntShiftState += 8;
    }
    result = CrntShiftDWord & CodeMasks[RunningBits];

    CrntShiftDWord >>= RunningBits;
    CrntShiftState -= RunningBits;

    /* If code cannot fit into RunningBits bits, must raise its size. Note
    * however that codes above 4095 are used for special signaling.
    * If we're using LZ_BITS bits already and we're at the max code, just
    * keep using the table as it is, don't increment Private->RunningCode.
    */
    if (RunningCode < LZ_MAX_CODE + 2 &&
      ++RunningCode > MaxCode1 &&
      RunningBits < LZ_BITS)
    {
      MaxCode1 <<= 1;
      RunningBits++;
    }
    return result;
  }

  /******************************************************************************
  Routine to trace the Prefixes linked list until we get a prefix which is
  not code, but a pixel value (less than ClearCode). Returns that pixel value.
  If image is defective, we might loop here forever, so we limit the loops to
  the maximum possible if image O.k. - LZ_MAX_CODE times.
  ******************************************************************************/
  GifWord GetPrefixChar(GifWord code, GifWord clearCode)
  {
    int i = 0;
    auto mutCode = code;
    while (mutCode > clearCode && i++ <= LZ_MAX_CODE)
    {
      if (mutCode > LZ_MAX_CODE)
      {
        return NO_SUCH_CODE;
      }
      mutCode = Prefix[mutCode];
    }
    return mutCode;
  }

  void DecompressLine(USERDATA& userData, GifPixelType *Line, int LineLen)
  {
    int i = 0, CrntCode;
    int CrntPrefix = 0;

    auto StackPtr = this->StackPtr;
    auto LastCode = this->LastCode;

    if (StackPtr > LZ_MAX_CODE)
    {
      throw std::runtime_error("blown stack pointer while decompressing");
    }

    if (StackPtr != 0)
    {
      /* Let pop the stack off before continueing to read the GIF file: */
      while (StackPtr != 0 && i < LineLen)
        Line[i++] = (GifPixelType)Stack[--StackPtr];
    }

    while (i < LineLen)
    {    /* Decode LineLen items. */
      CrntCode = DecompressInput(userData);
      if (CrntCode == EOFCode)
      {
        /* Note however that usually we will not be here as we will stop
        * decoding as soon as we got all the pixel, or EOF code will
        * not be read at all, and GetLine/Pixel clean everything.  */
        throw std::runtime_error("unexpected eof");
      }
      else if (CrntCode == ClearCode)
      {
        /* We need to start over again: */
        for (int j = 0; j <= LZ_MAX_CODE; j++)
          Prefix[j] = NO_SUCH_CODE;

        this->RunningCode = this->EOFCode + 1;
        this->RunningBits = this->BitsPerPixel + 1;
        this->MaxCode1 = 1 << this->RunningBits;
        LastCode = this->LastCode = NO_SUCH_CODE;
      }
      else
      {
        /* Its regular code - if in pixel range simply add it to output
        * stream, otherwise trace to codes linked list until the prefix
        * is in pixel range: */
        if (CrntCode < ClearCode)
        {
          /* This is simple - its pixel scalar, so add it to output: */
          Line[i++] = (GifPixelType)CrntCode;
        }
        else
        {
          /* Its a code to needed to be traced: trace the linked list
          * until the prefix is a pixel, while pushing the suffix
          * pixels on our stack. If we done, pop the stack in reverse
          * (thats what stack is good for!) order to output.  */
          if (Prefix[CrntCode] == NO_SUCH_CODE)
          {
            CrntPrefix = LastCode;

            /* Only allowed if CrntCode is exactly the running code:
            * In that case CrntCode = XXXCode, CrntCode or the
            * prefix code is last code and the suffix char is
            * exactly the prefix of last code! */
            if (CrntCode == this->RunningCode - 2)
            {
              Suffix[this->RunningCode - 2] =
                Stack[StackPtr++] = GetPrefixChar(LastCode, ClearCode);
            }
            else
            {
              Suffix[this->RunningCode - 2] =
                Stack[StackPtr++] = GetPrefixChar(CrntCode, ClearCode);
            }
          }
          else
            CrntPrefix = CrntCode;

          /* Now (if image is O.K.) we should not get a NO_SUCH_CODE
          * during the trace. As we might loop forever, in case of
          * defective image, we use StackPtr as loop counter and stop
          * before overflowing Stack[]. */
          while (StackPtr < LZ_MAX_CODE &&
            CrntPrefix > ClearCode && CrntPrefix <= LZ_MAX_CODE)
          {
            Stack[StackPtr++] = Suffix[CrntPrefix];
            CrntPrefix = Prefix[CrntPrefix];
          }
          if (StackPtr >= LZ_MAX_CODE || CrntPrefix > LZ_MAX_CODE)
          {
            throw std::runtime_error("recursion too deep while decompressing");
          }
          /* Push the last character on stack: */
          Stack[StackPtr++] = CrntPrefix;

          /* Now lets pop all the stack into output: */
          while (StackPtr != 0 && i < LineLen)
            Line[i++] = (GifPixelType)Stack[--StackPtr];
        }
        if (LastCode != NO_SUCH_CODE && Prefix[this->RunningCode - 2] == NO_SUCH_CODE)
        {
          Prefix[this->RunningCode - 2] = LastCode;

          if (CrntCode == this->RunningCode - 2)
          {
            /* Only allowed if CrntCode is exactly the running code:
            * In that case CrntCode = XXXCode, CrntCode or the
            * prefix code is last code and the suffix char is
            * exactly the prefix of last code! */
            Suffix[this->RunningCode - 2] = GetPrefixChar(LastCode, ClearCode);
          }
          else
          {
            Suffix[this->RunningCode - 2] = GetPrefixChar(CrntCode, ClearCode);
          }
        }
        LastCode = CrntCode;
      }
    }
    this->LastCode = LastCode;
    this->StackPtr = StackPtr;
  }

  void GetLine(USERDATA& userData, GifPixelType *line, int lineLen)
  {
    GifByteType *Dummy = nullptr;
    if (!lineLen)
      throw std::runtime_error("invalid line length");

    if ((PixelCount -= lineLen) > 0xffff0000UL)
    {
      throw std::runtime_error("data too big");
    }

    DecompressLine(userData, line, lineLen);
    if (PixelCount == 0)
    {
      /* We probably won't be called any more, so let's clean up
      * everything before we return: need to flush out all the
      * rest of image until an empty block (size 0)
      * detected. We use GetCodeNext.
      */

      while (GetCodeNext(userData, &Dummy));
    }
    
  }
};



template<typename UCALLBACK>
GifWord GetWord(UCALLBACK& callback)
{
  unsigned char c[2];

  if (callback.read(c, 2) != 2)
  {
    throw std::runtime_error("failed reading word");
  }

  return (GifWord)UNSIGNED_LITTLE_ENDIAN(c[0], c[1]);
}

template<typename UCALLBACK>
class GifFileType
{
    struct revertHelper
    {
    private:
        UCALLBACK& _userData;
    public:
        revertHelper(UCALLBACK& userData) : _userData(userData) {}
        revertHelper& operator=(const revertHelper &tmp) { _userData = tmp._userData; }
        ~revertHelper()
        {
            //revert the stream to the last known good read
            _userData.revert();
        }
        void checkpoint(int negativePosition = 0)
        {
            if (negativePosition != 0)
                _userData.retro_checkpoint(negativePosition);
            else
                _userData.checkpoint();
        }
    };

public:
  GifWord SWidth, SHeight;                  /* Size of virtual canvas */
  GifWord SColorResolution;                 /* How many colors can we generate? */
  GifWord SBackGroundColor;                 /* Background color for virtual canvas */
  GifByteType AspectByte;	                  /* Used to compute pixel aspect ratio */
  ColorMapObject SColorMap;                 /* Global colormap, NULL if nonexistent. */
  std::vector<SavedImage> SavedImages;         /* Image sequence (high-level API) */
  std::vector<ExtensionBlock> ExtensionBlocks; /* Extensions past last image */
  bool Gif89;
  GifFileType(UCALLBACK& userData)
  {
    revertHelper helper(userData);
    std::array<char, GIF_STAMP_LEN + 1> buf;
    /* Let's see if this is a GIF file: */
    if (userData.read((unsigned char *)&buf[0], GIF_STAMP_LEN) != GIF_STAMP_LEN)
    {
      throw std::runtime_error("failed reading gif stamp");
    }

    /* Check for GIF prefix at start of file */
    buf[GIF_STAMP_LEN] = 0;
    if (strncmp(GIF_STAMP, &buf[0], GIF_VERSION_POS) != 0)
    {
      throw std::runtime_error("not a gif");
    }

    /* What version of GIF? */
    Gif89 = (buf[GIF_VERSION_POS] == '9');

    /* Put the screen descriptor into the file: */
    SWidth = GetWord(userData);
    SHeight = GetWord(userData);

    if (userData.read((GifByteType*)&buf[0], 3) != 3)
    {
      throw std::runtime_error("failed to read screen descriptor");
    }
    SColorResolution = (((buf[0] & 0x70) + 1) >> 4) + 1;
    auto sortFlag = (buf[0] & 0x08) != 0;
    auto bitsPerPixel = (buf[0] & 0x07) + 1;
    SBackGroundColor = buf[1];
    AspectByte = buf[2];
    if (buf[0] & 0x80)
    {    /* Do we have global color map? */
      SColorMap.init(1 << bitsPerPixel);

      /* Get the global color map: */
      SColorMap.SortFlag = sortFlag;
      for (size_t i = 0; i < SColorMap.Colors.size(); i++)
      {
        if (userData.read((GifByteType*)&buf[0], 3) != 3)
        {
          throw std::runtime_error("invalid global color map");
        }
        SColorMap.Colors[i].Red = buf[0];
        SColorMap.Colors[i].Green = buf[1];
        SColorMap.Colors[i].Blue = buf[2];
      }
    }
    helper.checkpoint();
  }

  void Slurp(UCALLBACK& userData)
  {
	revertHelper helper(userData);
    std::vector<ExtensionBlock> localExtensionBlocks;
    for (;;)
    {
      switch (GetRecordType(userData))
      {
        case IMAGE_DESC_RECORD_TYPE:
        {
          ExtensionBlocks = localExtensionBlocks;
          SavedImages.emplace_back(LoadImage(userData));
          localExtensionBlocks.clear();
          helper.checkpoint();
          break;
        }

        case EXTENSION_RECORD_TYPE:
        {
          auto extensionBlock = GetExtension(userData);
          /* Create an extension block with our data */
          if (extensionBlock.Bytes.size() > 0)
          {
            localExtensionBlocks.push_back(extensionBlock);
          }
          while (extensionBlock.Bytes.size() > 0)
          {
            extensionBlock = GetExtensionNext(userData);
            localExtensionBlocks.push_back(extensionBlock);
          }
          
          break;
        }

        case TERMINATE_RECORD_TYPE:
          ExtensionBlocks = localExtensionBlocks;
          helper.checkpoint();
          return;
          break;

        default:    /* Should be trapped by DGifGetRecordType */
          break;
      }
    }
  }
private:
  SavedImage LoadImage(UCALLBACK& userData)
  {
    SavedImage image;
    image.ImageDesc = GetImageDesc(userData);
    GifDecompressor < typename UCALLBACK > decompressor(userData, image.ImageDesc.Width * image.ImageDesc.Height);
    if (image.ImageDesc.Width <= 0 || image.ImageDesc.Height <= 0 ||
      image.ImageDesc.Width >(INT_MAX / image.ImageDesc.Height) || 
      image.ImageDesc.Width > SWidth || image.ImageDesc.Height > SHeight)
    {
      throw std::runtime_error("invalid image descriptor");
    }
    auto imageSize = image.ImageDesc.Width * image.ImageDesc.Height;

    if (imageSize >(SIZE_MAX / sizeof(GifPixelType)))
    {
      throw std::runtime_error("invalid image descriptor");
    }
    image.RasterBits = std::make_unique<GifByteType[]>(imageSize);
    if (image.ImageDesc.Interlace)
    {
      /*
      * The way an interlaced image should be read -
      * offsets and jumps...
      */
      int InterlacedOffset[] = { 0, 4, 2, 1 };
      int InterlacedJumps[] = { 8, 8, 4, 2 };
      /* Need to perform 4 passes on the image */
      for (int i = 0; i < 4; i++)
        for (int j = InterlacedOffset[i];
          j < image.ImageDesc.Height;
          j += InterlacedJumps[i])
      {
        decompressor.GetLine(userData, image.RasterBits.get() + j*image.ImageDesc.Width, image.ImageDesc.Width);
      }
    }
    else
    {
      decompressor.GetLine(userData, image.RasterBits.get(), imageSize);
    }

    //this is pretty ugly but then again so is the format
    if (ExtensionBlocks.size() > 0)
    {
      image.ExtensionBlocks = ExtensionBlocks;
      ExtensionBlocks.clear();
    }
    return image;
  }

  GifRecordType GetRecordType(UCALLBACK& userData)
  {
    GifByteType Buf;
    if (userData.read(&Buf, 1) != 1)
    {
      throw std::runtime_error("failed to read record type");
    }

    switch (Buf)
    {
      case DESCRIPTOR_INTRODUCER:
        return IMAGE_DESC_RECORD_TYPE;
      case EXTENSION_INTRODUCER:
        return EXTENSION_RECORD_TYPE;
      case TERMINATOR_INTRODUCER:
        return TERMINATE_RECORD_TYPE;
      default:
        return UNDEFINED_RECORD_TYPE;
    }
  }

  ExtensionBlock GetExtension(UCALLBACK& userData)
  {
    GifByteType buf;
    if (userData.read(&buf, 1) != 1)
    {
      throw std::runtime_error("failed to read extension block");
    }
    auto extension = GetExtensionNext(userData);
    extension.Function = buf;
    return extension;
  }

  ExtensionBlock GetExtensionNext(UCALLBACK& userData)
  {
    ExtensionBlock extension;
    GifByteType buf;
    if (userData.read(&buf, 1) != 1)
    {
      throw std::runtime_error("failed to read extension block");
    }
    extension.Bytes.resize(buf);
    if (buf > 0)
    {
      if (userData.read(&extension.Bytes[0], buf) != buf)
      {
        throw std::runtime_error("failed to read extension block");
      }
    }

    return extension;
  }

  GifImageDesc GetImageDesc(UCALLBACK& userData)
  {
    //left, top, width, height
    auto imageDesc = GifImageDesc{ GetWord(userData), GetWord(userData), GetWord(userData), GetWord(userData) };
    GifByteType buf[3];
    if (userData.read(buf, 1) != 1)
    {
      throw std::runtime_error("failed to get bits per pixel");
    }
    unsigned int bitsPerPixel = (buf[0] & 0x07) + 1;
    imageDesc.Interlace = (buf[0] & 0x40) ? true : false;

    /* Does this image have local color map? */
    if (buf[0] & 0x80)
    {
      unsigned int i;

      imageDesc.ColorMap.init(1 << bitsPerPixel);

      /* Get the image local color map: */
      for (i = 0; i < imageDesc.ColorMap.Colors.size(); i++)
      {
        if (userData.read(buf, 3) != 3)
        {
          throw std::runtime_error("failed to get local color map");
        }
        imageDesc.ColorMap.Colors[i].Red = buf[0];
        imageDesc.ColorMap.Colors[i].Green = buf[1];
        imageDesc.ColorMap.Colors[i].Blue = buf[2];
      }
    }

    return imageDesc;
  }
};

#define D_GIF_SUCCEEDED          0
#define D_GIF_ERR_OPEN_FAILED    101    /* And DGif possible errors. */
#define D_GIF_ERR_READ_FAILED    102
#define D_GIF_ERR_NOT_GIF_FILE   103
#define D_GIF_ERR_NO_SCRN_DSCR   104
#define D_GIF_ERR_NO_IMAG_DSCR   105
#define D_GIF_ERR_NO_COLOR_MAP   106
#define D_GIF_ERR_WRONG_RECORD   107
#define D_GIF_ERR_DATA_TOO_BIG   108
#define D_GIF_ERR_NOT_ENOUGH_MEM 109
#define D_GIF_ERR_CLOSE_FAILED   110
#define D_GIF_ERR_NOT_READABLE   111
#define D_GIF_ERR_IMAGE_DEFECT   112
#define D_GIF_ERR_EOF_TOO_SOON   113


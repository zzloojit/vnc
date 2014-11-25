#include <stdio.h>
#include <assert.h>

void dump_bitmap(int width, int height, unsigned short bpp, unsigned char* data) 
{
  char file_str[256];
  FILE* f;
  static id = 0;
  id++;
  sprintf(file_str, "/tmp/tmpfs/%d.bmp", id, width, height);
  unsigned int header_size = 14 + 40;
  unsigned int bitmap_data_offset= header_size;
  unsigned int row_size = width * bpp / 8;
  unsigned int file_size = bitmap_data_offset + row_size * height;
  unsigned short tmp_u16 = 0;
  unsigned int tmp_u32 = 0;

  f = fopen(file_str, "wb");
  if (!f) {
    assert (0 && "error creating bmp");
    return;
  }
  
  fprintf(f, "BM");
  fwrite(&file_size, sizeof(file_size), 1, f);
  tmp_u16 = 0;
  fwrite(&tmp_u16, sizeof(tmp_u16), 1, f);
  fwrite(&tmp_u16, sizeof(tmp_u16), 1, f);
  fwrite(&bitmap_data_offset, sizeof(bitmap_data_offset), 1, f);
  
  tmp_u32 = header_size - 14;
  fwrite(&tmp_u32, sizeof(tmp_u32), 1, f);
  tmp_u32 = width;
  fwrite(&tmp_u32, sizeof(tmp_u32), 1, f);
  tmp_u32 = height;
  fwrite(&tmp_u32, sizeof(tmp_u32), 1, f);

  tmp_u16 = 0;
  fwrite (&tmp_u16, sizeof(tmp_u16), 1, f);
  fwrite (&bpp, sizeof(bpp), 1, f);

  tmp_u32 = 0;
  fwrite (&tmp_u32, sizeof(tmp_u32), 1, f); // compression method
  
  tmp_u32 = 0;
  fwrite(&tmp_u32, sizeof(tmp_u32), 1, f);
  fwrite(&tmp_u32, sizeof(tmp_u32), 1, f);
  fwrite(&tmp_u32, sizeof(tmp_u32), 1, f);
  fwrite(&tmp_u32, sizeof(tmp_u32), 1, f); // plt entries
  fwrite(&tmp_u32, sizeof(tmp_u32), 1, f);

  fwrite(data, 1, width * height * bpp, f);
  fclose(f);
} 

#pragma once

#include "imgui.h"

// Glyphs from thirdparty/icon/demo.html (fontello, updated library)
namespace icons
{
    inline constexpr const char* pitch              = "2";
    inline constexpr const char* eyedropper         = "C";
    inline constexpr const char* brush_alt          = "E";
    inline constexpr const char* cog_6              = "c";
    inline constexpr const char* search_7           = "\xE2\x9C\x95"; // 0x2715
    inline constexpr const char* pipette            = "\xEE\x80\xBB"; // 0xe03b
    inline constexpr const char* art_gallery        = "\xEE\x81\x84"; // 0xe044 color
    inline constexpr const char* target_3           = "\xEE\x81\x8E"; // 0xe04e
    inline constexpr const char* power              = "\xEE\x81\x93"; // 0xe053
    inline constexpr const char* pencil_2           = "\xEE\x81\x9F"; // 0xe05f
    inline constexpr const char* flash_outline      = "\xEE\x81\xAC"; // 0xe06c
    inline constexpr const char* flash_1            = "\xEE\x81\xBE"; // 0xe07e
    inline constexpr const char* ok_2               = "\xEE\x82\x9C"; // 0xe09c
    inline constexpr const char* search             = "\xEE\x82\xB2"; // 0xe0b2
    inline constexpr const char* cog                = "\xEE\x82\xB7"; // 0xe0b7
    inline constexpr const char* clock              = "\xEE\x83\x8D"; // 0xe0cd
    inline constexpr const char* cloud_sun          = "\xEE\x84\x82"; // 0xe102
    inline constexpr const char* target             = "\xEE\xA0\x84"; // 0xe804
    inline constexpr const char* lock_2             = "\xEE\xA7\x88"; // 0xe9c8
    inline constexpr const char* eye_6              = "\xEE\xA7\x8C"; // 0xe9cc
    inline constexpr const char* eye_off            = "\xEE\xA7\x8D"; // 0xe9cd
    inline constexpr const char* menu_1             = "\xEE\xA8\xA8"; // 0xea28
    inline constexpr const char* home_6             = "\xEE\xA8\xB8"; // 0xea38
    inline constexpr const char* download_4         = "\xEE\xA9\x84"; // 0xea44
    inline constexpr const char* keyboard           = "\xEE\xA9\x91"; // 0xea51
    inline constexpr const char* folder_3           = "\xEE\xA9\xA7"; // 0xea67
    inline constexpr const char* box_2              = "\xEE\xA9\xA9"; // 0xea69
    inline constexpr const char* cog_7              = "\xEE\xA9\xAC"; // 0xea6c
    inline constexpr const char* adjust_2           = "\xEE\xA9\xBE"; // 0xea7e
    inline constexpr const char* chevron_down       = "\xEE\xAA\x8A"; // 0xea8a
    inline constexpr const char* chevron_right      = "\xEE\xAA\x8C"; // 0xea8c
    inline constexpr const char* palette            = "\xEE\xAA\xBB"; // 0xeabb
    inline constexpr const char* mouse              = "\xEE\xAB\x91"; // 0xead1
    inline constexpr const char* dot_3              = "\xEE\xAB\x96"; // 0xead6
    inline constexpr const char* brush_2            = "\xEE\xAB\x97"; // 0xead7
    inline constexpr const char* droplet            = "\xEE\xAB\xA4"; // 0xeae4
    inline constexpr const char* key_2              = "\xEE\xAB\xAE"; // 0xeaee
    inline constexpr const char* lock_5             = "\xEE\xAC\xA8"; // 0xeb28
    inline constexpr const char* attention_4        = "\xEE\xAC\xBE"; // 0xeb3e risky
    inline constexpr const char* folder_open_2      = "\xEE\xAD\x84"; // 0xeb44
    inline constexpr const char* cog_alt            = "\xEE\xAD\x87"; // 0xeb47
    inline constexpr const char* wrench_3           = "\xEE\xAD\x88"; // 0xeb48
    inline constexpr const char* umbrella_1         = "\xEE\xAE\x81"; // 0xeb81
    inline constexpr const char* off_1              = "\xEE\xAE\x92"; // 0xeb92
    inline constexpr const char* sliders            = "\xEF\x87\x9E"; // 0xf1de
    inline constexpr const char* code_2             = "\xEF\x84\xA1"; // 0xf121
    inline constexpr const char* user_secret        = "\xEF\x88\x9B"; // 0xf21b
    inline constexpr const char* bell               = "\xEF\x83\xB3"; // 0xf0f3

    inline constexpr const char* key               = key_2;
    inline constexpr const char* check              = ok_2;
    inline constexpr const char* close              = search_7;
    inline constexpr const char* exit               = off_1;
    inline constexpr const char* color              = art_gallery;
    inline constexpr const char* risky              = attention_4;
    inline constexpr const char* aim                = target_3;
    inline constexpr const char* eye                = eye_6;
    inline constexpr const char* home               = home_6;
    inline constexpr const char* settings           = cog_7;
    inline constexpr const char* save               = download_4;
    inline constexpr const char* load               = folder_open_2;
    inline constexpr const char* shuffle            = user_secret;
    inline constexpr const char* arrow              = chevron_right;
}

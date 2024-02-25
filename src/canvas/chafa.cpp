// Display images inside a terminal
// Copyright (C) 2023  JustKidding
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "chafa.hpp"
#include "dimensions.hpp"
#include "terminal.hpp"
#include "util.hpp"
#include "util/ptr.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <range/v3/all.hpp>
#include <spdlog/spdlog.h>

void gstring_delete(GString *str)
{
    g_string_free(str, true);
}

Chafa::Chafa(std::unique_ptr<Image> new_image, std::shared_ptr<std::mutex> stdout_mutex)
    : symbol_map(chafa_symbol_map_new()),
      config(chafa_canvas_config_new()),
      image(std::move(new_image)),
      stdout_mutex(std::move(stdout_mutex))
{
    const auto envp = c_unique_ptr<gchar *, g_strfreev>{g_get_environ()};
    term_info = chafa_term_db_detect(chafa_term_db_get_default(), envp.get());

    const auto dims = image->dimensions();
    x = dims.x + 1;
    y = dims.y + 1;
    horizontal_cells = std::ceil(static_cast<double>(image->width()) / dims.terminal->font_width);
    vertical_cells = std::ceil(static_cast<double>(image->height()) / dims.terminal->font_height);

    chafa_symbol_map_add_by_tags(symbol_map, CHAFA_SYMBOL_TAG_BLOCK);
    chafa_symbol_map_add_by_tags(symbol_map, CHAFA_SYMBOL_TAG_BORDER);
    chafa_symbol_map_add_by_tags(symbol_map, CHAFA_SYMBOL_TAG_SPACE);
    chafa_symbol_map_remove_by_tags(symbol_map, CHAFA_SYMBOL_TAG_WIDE);
    chafa_canvas_config_set_symbol_map(config, symbol_map);
    chafa_canvas_config_set_pixel_mode(config, CHAFA_PIXEL_MODE_SYMBOLS);
    chafa_canvas_config_set_geometry(config, horizontal_cells, vertical_cells);
}

Chafa::~Chafa()
{
    chafa_canvas_unref(canvas);
    chafa_canvas_config_unref(config);
    chafa_symbol_map_unref(symbol_map);
    chafa_term_info_unref(term_info);

    const std::scoped_lock lock{*stdout_mutex};
    util::clear_terminal_area(x, y, horizontal_cells, vertical_cells);
}

void Chafa::draw()
{
    canvas = chafa_canvas_new(config);
    chafa_canvas_draw_all_pixels(canvas, CHAFA_PIXEL_BGRA8_UNASSOCIATED, image->data(), image->width(), image->height(),
                                 image->width() * 4);

#ifdef CHAFA_VERSION_1_14
    GString **lines = nullptr;
    gint lines_length = 0;

    chafa_canvas_print_rows(canvas, term_info, &lines, &lines_length);
    auto ycoord = y;
    const std::scoped_lock lock{*stdout_mutex};
    util::save_cursor_position();
    for (int i = 0; i < lines_length; ++i) {
        const auto line = c_unique_ptr<GString, gstring_delete>{lines[i]};
        util::move_cursor(ycoord++, x);
        std::cout << line->str;
    }
    g_free(lines);
#else
    const auto result = c_unique_ptr<GString, gstring_delete>{chafa_canvas_print(canvas, term_info)};
    auto ycoord = y;
    const auto lines = util::str_split(result->str, "\n");

    const std::scoped_lock lock{*stdout_mutex};
    util::save_cursor_position();
    ranges::for_each(lines, [this, &ycoord](const std::string &line) {
        util::move_cursor(ycoord++, x);
        std::cout << line;
    });
#endif
    std::cout << std::flush;
    util::restore_cursor_position();
}

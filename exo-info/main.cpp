// SPDX-FileCopyrightText: 2025 David Andrs <andrsd@gmail.com>
// SPDX-License-Identifier: MIT

#include "common.h"
#include "cxxopts/cxxopts.hpp"
#include <exodusIIcpp/exodusIIcpp.h>
#include <fmt/core.h>

void
print_cell_set_info(const exodusIIcpp::File & exo)
{
    const auto & blocks = exo.get_element_blocks();
    if (blocks.size() > 0) {
        fmt::print("\n");
        fmt::print("Cell sets [{}]:\n", blocks.size());
        std::size_t wd_id = 1;
        std::size_t wd_name = 1;
        std::size_t wd_num = 1;
        for (const auto & eb : blocks) {
            wd_id = std::max(wd_id, fmt::format("{}", eb.get_id()).size());
            auto name = eb.get_name();
            if (name.size() == 0)
                name = "<no name>";
            wd_name = std::max(wd_name, fmt::format("{}", name).size());
            wd_num =
                std::max(wd_num, fmt::format("{}", human_number(eb.get_num_elements())).size());
        }
        wd_name++;

        for (const auto & eb : blocks) {
            // auto elem_counts = element_counts(mesh, id);
            auto id = eb.get_id();

            fmt::print("- {:>{}}: ", id, wd_id);

            auto name = eb.get_name();
            if (name.size() == 0)
                name = "<no name>";
            fmt::print("{:<{}} ", name, wd_name);

            fmt::print("{:>{}} elements ", human_number(eb.get_num_elements()), wd_num);
            fmt::print(" (");
            auto etyp = element_type(eb.get_element_type());
            fmt::print("{}", element_type_str(etyp));
            fmt::print(")");
            fmt::print("\n");
        }
    }
}

void
print_side_set_info(const exodusIIcpp::File & exo)
{
    // side sets
    if (exo.get_num_side_sets() > 0) {
        std::size_t wd_id = 1;
        std::size_t wd_name = 1;
        std::size_t wd_num = 1;
        for (const auto & ss : exo.get_side_sets()) {
            wd_id = std::max(wd_id, fmt::format("{}", ss.get_id()).size());
            auto name = ss.get_name();
            if (name.size() == 0)
                name = "<no name>";
            wd_name = std::max(wd_name, fmt::format("{}", name).size());
            wd_num = std::max(wd_num, fmt::format("{}", human_number(ss.get_size())).size());
        }
        wd_name++;

        fmt::print("\n");
        fmt::print("Side sets [{}]:\n", exo.get_num_side_sets());

        for (const auto & ss : exo.get_side_sets()) {
            auto id = ss.get_id();
            fmt::print("- {:>{}}: ", id, wd_id);
            auto name = ss.get_name();
            if (name.size() == 0)
                name = "<no name>";
            fmt::print("{:<{}} ", name, wd_name);

            fmt::print("{:>{}} sides\n", human_number(ss.get_size()), wd_num);
        }
    }
}

void
print_mesh_info(const std::string & filename)
{
    fmt::print("Reading file: {}...", filename);
    std::fflush(stdout);
    exodusIIcpp::File exo(filename, exodusIIcpp::FileAccess::READ);
    exo.read_blocks();
    exo.read_side_sets();
    fmt::print(" done\n");

    fmt::print("\n");
    fmt::print("Global:\n");
    fmt::print("- {} elements\n", human_number(exo.get_num_elements()));
    fmt::print("- {} nodes\n", human_number(exo.get_num_nodes()));

    print_cell_set_info(exo);
    print_side_set_info(exo);
}

int
main(int argc, char * argv[])
{
    cxxopts::Options options("exo-info", "Display information about an exodusII file");
    // clang-format off
    options.add_options()
        ("filename", "The mesh file name", cxxopts::value<std::string>())
        ("h,help", "Print usage")
    ;
    // clang-format on
    options.parse_positional({ "filename" });

    auto result = options.parse(argc, argv);
    if (result["filename"].count())
        print_mesh_info(result["filename"].as<std::string>());
    else {
        fmt::print("{}\n", options.help());
    }
    return 0;
}

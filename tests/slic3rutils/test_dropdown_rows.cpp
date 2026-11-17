#include <catch2/catch_test_macros.hpp>

#include "slic3r/GUI/Widgets/DropDown.hpp"

#include <wx/gdicmn.h>

TEST_CASE("A list row is under the pointer only inside the list", "[DropDown][Regression]")
{
    const wxSize client(200, 150); // five rows of 30 shown

    // Inside: the row under the point, counting the rows scrolled past.
    CHECK(dropdown_row_at({10, 0}, client, 0, 30, 20) == 0);
    CHECK(dropdown_row_at({10, 149}, client, 0, 30, 20) == 4);
    CHECK(dropdown_row_at({10, 45}, client, -90, 30, 20) == 4);

    // The 2026-11-17 report: a stray click with the printer list open chose a printer. On macOS the
    // open list sees the pointer outside itself, and its row followed the pointer's height alone.
    CHECK(dropdown_row_at({-5, 45}, client, 0, 30, 20) == -1);
    CHECK(dropdown_row_at({200, 45}, client, 0, 30, 20) == -1);
    // Just above the list, where the division rounded to row 0, and just below it.
    CHECK(dropdown_row_at({10, -5}, client, 0, 30, 20) == -1);
    CHECK(dropdown_row_at({10, 150}, client, 0, 30, 20) == -1);
    // Below the last row of a short list, and before the rows are measured.
    CHECK(dropdown_row_at({10, 100}, client, 0, 30, 3) == -1);
    CHECK(dropdown_row_at({10, 10}, client, 0, 0, 3) == -1);
}

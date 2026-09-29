#ifndef TEST_PAGE_H
#define TEST_PAGE_H

// Renders a built-in diagnostic page through the print service.
// Call from a worker task; this can block while the printer queue drains.
bool print_test_page();

#endif

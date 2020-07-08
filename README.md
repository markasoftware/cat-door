# cat-door

KiCAD files and ATtiny84 firmware for my [cat door](https://markasoftware.com/cat-door).

The state machine diagram is old and incorrect. The unit tests in `test.lisp` do
not work, partially because they are written rong, and partially because simavr
seems to have bugs related to inputs with pull-up resistors.

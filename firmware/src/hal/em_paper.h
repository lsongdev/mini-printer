#ifndef EM_PAPER_H
#define EM_PAPER_H

// Paper sensor on the original V4 board: LOW means paper is present.
void paper_init();
bool paper_present();
// Returns true once per observed GPIO change. Call from a task, not an ISR.
bool paper_take_change();

#endif

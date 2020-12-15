//
//	virt_mem.c
//	virt_mem
//
//	On line 33 set FRAME_NUM to 256 to not used replacement policy, or 128 to use replacement policy
//	On line 168 set replace_policy to 0 for FIFO or 1 for LRU
//	On line 322 can uncomment assert if using FRAME_NUM of 256

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#pragma warning(disable : 4996)

#define ARGC_ERROR 1
#define FILE_ERROR 2

// CHANGE REPLACE_POLICY to LRU when wanting to use least-recently-used page replacement strategy
#define FIFO  0
#define LRU   1
#define REPLACE_POLICY  FIFO

// SET FRAMES TO 128 to use replacement policy: FIFO or LRU
#define FRAMES 128
#define FRAME_SIZE  256

#define PTABLE_SIZE 256
#define TLB_SIZE 16

typedef struct pg_table_t {
  unsigned int page_num;
  unsigned int frame_num;
	bool present;
	bool used;
} pg_table_t;

//-------------------------------------------------------------------
unsigned int get_page(size_t x)         { return (0xff00 & x) >> 8; }
unsigned int get_offset(unsigned int x) { return (0xff & x); }

void getpage_offset(unsigned int x) {
	unsigned int page   = get_page(x);
	unsigned int offset = get_offset(x);
	printf("x is: %u, page: %u, offset: %u, address: %u, paddress: %u\n", x, page, offset,
		(page << 8) | get_offset(x), page * 256 + offset);
}

void ptable_update_frame(pg_table_t* pt, unsigned int page_num, unsigned int frame_num) {
  pt[page_num].frame_num = frame_num;
  pt[page_num].present = true;
  pt[page_num].used = true;
}

// For FIFO replacement policy
// Returns the page number of a Page Table entry with a matching frame
int ptable_find_frame(pg_table_t* pt, unsigned int frame) {

	for (int i = 0; i < PTABLE_SIZE; i++) {
		if (pt[i].frame_num == frame && pt[i].present == true) {
			return i;
		}
	}
	return -1;  // no match
}

// For LRU replacement policy
unsigned int ptable_get_used(pg_table_t* pt) {
	for (int i = 0; i < PTABLE_SIZE; i++) {
    if (!pt[i].used && pt[i].present) { return (unsigned int)i; }
	}
	// All present pages have been used recently, set all page entry used flags to false
	for (int i = 0; i < PTABLE_SIZE; i++) {
		pt[i].used = false;
	}
	for (int i = 0; i < PTABLE_SIZE; i++) {
		if (!pt[i].used && pt[i].present) { return (unsigned int)i; }
	}

	return (unsigned int)-1;
}

// Returns the index of a TLB entry with a matching page
int tlb_check(pg_table_t* tlb, unsigned int page) {
	for (int i = 0; i < TLB_SIZE; i++) {
    if (tlb[i].page_num == page) { return i; }
	}
	return -1;  // no match
}

void tlb_add(pg_table_t* tlb, int index, pg_table_t entry) { tlb[index] = entry; }

void tlb_remove(pg_table_t* tlb, int index) {
  tlb[index].page_num = (unsigned int)-1;
  tlb[index].present = false;
}

void files_open(FILE** fadd, FILE** fcorr, FILE** fback) {
  *fadd = fopen("addresses.txt", "r");
  if (*fadd == NULL) { fprintf(stderr, "Could not open file: 'addresses.txt'\n");  exit(FILE_ERROR); }

  *fcorr = fopen("correct.txt", "r");
  if (*fcorr == NULL) { fprintf(stderr, "Could not open file: 'correct.txt'\n");  exit(FILE_ERROR); }

  *fback = fopen("BACKING_STORE.bin", "rb");
  if (*fback == NULL) { fprintf(stderr, "Could not open file: 'BACKING_STORE.bin'\n");  exit(FILE_ERROR); }
}

void files_close(FILE* fadd, FILE* fcorr, FILE* fback) {
  fclose(fadd);
  fclose(fcorr);
  fclose(fback);
}

void run_virtual_memory() {
  FILE *fadd, *fcorr, *fback;
  files_open(&fadd, &fcorr, &fback);

  char buf[BUFSIZ];
  unsigned int page, offset, physical_add, frame = 0, val = 0;
  unsigned int logic_add;                  // from file address.txt
  unsigned int virt_add, phys_add, value;  // from file correct.txt

  int frames_used = 0;
  bool mem_full = false;

  int pg_fault = 0, tlb_hit = 0;
  char* frame_ptr = (char*)malloc(FRAMES * FRAME_SIZE);

  pg_table_t pgtable[PTABLE_SIZE];

  // Initialize Page Table Entries
  for (int i = 0; i < PTABLE_SIZE; i++) {
    pgtable[i].page_num = (unsigned int)i;
    pgtable[i].present = false;
    pgtable[i].used = false;
  }

  // TLB contains Page Table Entries
  pg_table_t tlb[TLB_SIZE];

  // Initialize TLB Entries
  for (int i = 0; i < TLB_SIZE; i++) {
    tlb[i].page_num = (unsigned int)-1;
    tlb[i].present = false;
    pgtable[i].used = false;
  }

  int tlb_idx = 0;

  for (int i = 0; i < 1000; ++i) {
    fscanf(fcorr, "%s %s %d %s %s %d %s %d", buf, buf, &virt_add,
      buf, buf, &phys_add, buf, &value);

    fscanf(fadd, "%d", &logic_add);
    page   = get_page(logic_add);
    offset = get_offset(logic_add);

    int result = tlb_check(tlb, page);
    if (result >= 0) {  //TLB check
      ++tlb_hit;

      frame = tlb[result].frame_num;
      page = tlb[result].page_num;
      pgtable[page].used = true;
    }
    else if (pgtable[page].present) {
      frame = pgtable[page].frame_num;    // set frame
      pgtable[page].used = true;
      tlb_add(tlb, tlb_idx++, pgtable[page]);
      tlb_idx %= TLB_SIZE;
    }
    else {
      ++pg_fault;
      if (frames_used >= FRAMES) {
        if (REPLACE_POLICY == FIFO)  { frames_used = 0; }   // FIFO
        mem_full = true;
      }

      frame = frames_used;
      if (mem_full) {
        if (REPLACE_POLICY == FIFO) {  // FIFO
          int pg = ptable_find_frame(pgtable, frame);
          if (pg != -1) { pgtable[pg].present = false; }

          int entry = tlb_check(tlb, (unsigned int)pg);
          if (entry != -1) { tlb_remove(tlb, entry); }
        }
        else {
          // TODO:  LRU
          // Get a page that hasn't been used recently
          unsigned int pg = ptable_get_used(pgtable);

          // Get frame from unused page entry and set frame for physical address
          frame = pgtable[pg].frame_num;
          pgtable[pg].present = false;

          int entry = tlb_check(tlb, (unsigned int)pg);
          if (entry != -1) { tlb_remove(tlb, entry); }
        }
      }
      fseek(fback, page * FRAME_SIZE, SEEK_SET);    // Load page into memory, update pgtable and tlb
      fread(buf, FRAME_SIZE, 1, fback);

      for (int i = 0; i < FRAME_SIZE; i++) {
        *(frame_ptr + (frame * FRAME_SIZE) + i) = buf[i];
      }

      ptable_update_frame(pgtable, page, frame);
      tlb_add(tlb, tlb_idx++, pgtable[page]);
      tlb_idx %= TLB_SIZE;
      ++frames_used;
    }

    physical_add = (frame * FRAME_SIZE) + offset;
    val = *(frame_ptr + physical_add);

    //assert(physical_add == phys_add); //Comment if using FRAMES < PTABLE_SIZE
    assert(val == value);
    // todo: read BINARY_STORE and confirm value matches read value from correct.txt
    printf("logical: %5u (page:%3u, offset:%3u) ---> physical: %5u (frame: %3u)---> value: %4d -- passed\n", logic_add, page, offset, physical_add, frame, (signed int)val);
    if (i % 5 == 4) { printf("\n"); }
  }

  printf("\nPage Fault: %1.3f%%", (double)pg_fault / 1000);
  printf("\nTLB Hit: %1.3f%%\n\n", (double)tlb_hit / 1000);
  printf("ALL logical ---> physical assertions PASSED!\n");
  printf("\n\t\t...done.\n");

  files_close(fadd, fcorr, fback);
  free(frame_ptr);
}

int main(int argc, const char * argv[]) {
  run_virtual_memory();

  printf("Hello World!");

	return 0;
}

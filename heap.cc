#include "heap.h"

#include "new.h"
#include "screen.h"

#define HEAP_INDEX_SIZE     0x20000
#define HEAP_MAGIC          0x0ABBCCD0
#define HEAP_MIN_SIZE       0x70000

// static
Heap* Heap::Create(uint32_t start, uint32_t end_addr, uint32_t max,
                   bool supervisor, bool readonly) {
  if (start%0x1000 != 0)
    PANIC("start not page aligned");
  if (end_addr%0x1000 != 0)
    PANIC("end not page aligned");

  return new(kalloc(sizeof(Heap))) Heap(start, end_addr, max, supervisor, readonly);
}

// static
void Heap::Destroy(Heap* heap) {
  heap->~Heap();
  kfree(heap);
}

void* Heap::Alloc(uint32_t size, bool page_align) {
  uint32_t new_size = size + sizeof(Header) + sizeof(Footer);
  uint32_t iterator = FindSmallestHole(new_size, page_align);
  if (iterator == (uint32_t) -1) {
    // Didn't find a hole
    uint32_t old_length = end_address - start_address;
    uint32_t old_end_address = end_address;

    Expand(old_length + new_size);
    uint32_t new_length = end_address - start_address;

    iterator = 0;
    int32_t idx = -1; uint32_t value = 0x0;
    while (iterator < index.size()) {
      uint32_t tmp = (uint32_t) index.Lookup(iterator);
      if (tmp > value) {
        value = tmp;
        idx = iterator;
      }
      ++iterator;
    }

    // If we didn't find ANY headers, we need to add one.
    if (idx == -1) {
      Header* header = (Header*) old_end_address;
      header->magic = HEAP_MAGIC;
      header->size = new_length - old_length;
      header->is_hole = 1;

      Footer* footer = (Footer*) (old_end_address + header->size - sizeof(Footer));
      footer->magic = HEAP_MAGIC;
      footer->header = header;
      index.Insert(header);
    } else {
      // Adjust the last header
      Header* header = index.Lookup(idx);
      header->size += new_length - old_length;

      // Rewrite the footer
      Footer* footer = (Footer*) ((uint32_t)header + header->size - sizeof(Footer));
      footer->header = header;
      footer->magic = HEAP_MAGIC;
    }
    // We now have enough space - recurse and try again.
    return Alloc(size, page_align);
  }
  Header* orig_header = (Header*) index.Lookup(iterator);
  uint32_t orig_pos = (uint32_t) orig_header;
  uint32_t orig_size = orig_header->size;
  // Should we split the hole?
  if (orig_size - new_size < sizeof(Header) + sizeof(Footer)) {
    // Just increase the requested size
    size += orig_size - new_size;
    new_size = orig_size;
  }

  // If we need to page align, make a new hole
  if (page_align && (orig_pos & 0xFFFFF000)) {
    uint32_t new_location = orig_pos + 0x1000 - (orig_pos & 0xFFF) - sizeof(Header);
    Header* hole_header = (Header*) orig_pos;
    hole_header->size = 0x1000 - (orig_pos&0xFFF) - sizeof(Header);
    hole_header->magic = HEAP_MAGIC;
    hole_header->is_hole = 1;

    Footer* hole_footer = (Footer*) ((uint32_t)new_location - sizeof(Footer));
    hole_footer->magic = HEAP_MAGIC;
    hole_footer->header = hole_header;
    orig_pos = new_location;
    orig_size = orig_size - hole_header->size;
  } else {
    index.Remove(iterator);
  }

  Header* block_header = (Header*) orig_pos;
  block_header->magic = HEAP_MAGIC;
  block_header->is_hole = 0;
  block_header->size = new_size;

  Footer* block_footer = (Footer*) (orig_pos + sizeof(Header) + size);
  block_footer->magic = HEAP_MAGIC;
  block_footer->header = block_header;

  // Do we need a new hole after the block?
  if (orig_size - new_size > 0) {
    Header* hole_header = (Header*) (orig_pos + sizeof(Header) + size + sizeof(Footer));
    hole_header->magic = HEAP_MAGIC;
    hole_header->is_hole = 1;
    hole_header->size = orig_size - new_size;

    Footer* hole_footer = (Footer*) ((uint32_t)hole_header + orig_size - new_size - sizeof(Footer));
    if ((uint32_t)hole_footer < end_address) {
      hole_footer->magic = HEAP_MAGIC;
      hole_footer->header = hole_header;
    }
    index.Insert(hole_header);
  }
  return (void*)((uint32_t)block_header + sizeof(Header));
}

void Heap::Free(void* p) {
  if (p == 0)
    return;

  Header* header = (Header*) ((uint32_t) p - sizeof(Header));
  Footer* footer = (Footer*) ((uint32_t)header + header->size - sizeof(Footer));

  if (header->magic != HEAP_MAGIC) PANIC("heap corrupted");
  if (footer->magic != HEAP_MAGIC) PANIC("heap corrupted");

  header->is_hole = 1;

  bool do_add = true;

  // Coalesce left
  Footer* test_footer = (Footer*) ((uint32_t) header - sizeof(Footer));
  if (test_footer->magic == HEAP_MAGIC && test_footer->header->is_hole == 1) {
    uint32_t cache_size = header->size;
    header = test_footer->header;
    footer->header = header;
    header->size += cache_size;
    do_add = false;
  }

  // Coalesce right
  Header* test_header = (Header*) ((uint32_t)footer + sizeof(Footer));
  if (test_header->magic == HEAP_MAGIC && test_header->is_hole) {
    header->size += test_header->size;
    test_footer = (Footer*)((uint32_t)test_header + test_header->size - sizeof(Footer));
    footer = test_footer;
    uint32_t iterator = 0;
    while ((iterator < index.size()) && index.Lookup(iterator) != test_header)
      ++iterator;

    if (iterator >= index.size()) PANIC("Failed to find block");

    index.Remove(iterator);
  }

  // Can we contract?
  /*
  if ((uint32_t)footer + sizeof(Footer) == end_address) {
    uint32_t old_length = end_address - start_address;
    uint32_t new_length = Contract((uint32_t)header - start_address);
    if (header->size - (old_length - new_length) > 0) {
      header->size -= old_length - new_length;
      footer = (Footer*) ((uint32_t)header + header->size - sizeof(Footer));
      footer->magic = HEAP_MAGIC;
      footer->header = header;
    } else {
      uint32_t iterator = 0;
      while ((iterator < index.size()) && (index.Lookup(iterator) != test_header))
        ++iterator;
      if (iterator < index.size())
        index.Remove(iterator);
    }
  }*/

  if (do_add)
    index.Insert(header);
}

// static
bool Heap::HeaderLessThan(Header* const& a, Header* const& b) {
  return a->size < b->size;
}

Heap::Heap(uint32_t start, uint32_t end_addr, uint32_t max, bool supervisor, bool readonly)
    : index((void*) start, HEAP_INDEX_SIZE, HeaderLessThan),
      end_address(end_addr),
      max_address(max),
      supervisor(supervisor),
      readonly(readonly) {
  // Allow for index array.
  start += sizeof(Header*) * HEAP_INDEX_SIZE;
  if ((start & 0xFFFFF000) != 0) {
    start &= 0xFFFFF000;
    start += 0x1000;
  }
  start_address = start;

  Header* hole = (Header*) start;
  hole->size = end_addr - start;
  hole->magic = HEAP_MAGIC;
  hole->is_hole = 1;
  index.Insert(hole);
}

Heap::~Heap() {
}

int32_t Heap::FindSmallestHole(uint32_t size, bool page_align) const {
  uint32_t iterator = 0;
  while (iterator < index.size()) {
    Header* header = index.Lookup(iterator);
    if (page_align) {
      uint32_t location = (uint32_t) header;
      int32_t offset = 0;
      if (((location + sizeof(Header)) & 0xFFFFF000) != 0)
        offset = 0x1000 - (location + sizeof(Header))%0x1000;
      int32_t hole_size = (int32_t)header->size - offset;
      if (hole_size >= (int32_t) size)
        break;
    } else if (header->size >= size)
      break;
    ++iterator;
  }

  if (iterator != index.size())
    return iterator;
  return -1;
}

void Heap::Expand(uint32_t new_size) {
  PANIC("Need to expand");
  //if (new_size <= end_address - start_address)
  //  PANIC("Bad size");
  //if ((new_size & 0xFFFFF000) != 0) {
  //  new_size &= 0xFFFFF000;
  //  new_size += 0x1000;
  //}

  //if (start_address + new_size > max_address)
  //  PANIC("Over capacity");

  //uint32_t old_size = end_address - start_address;
  //uint32_t i = old_size;
  //while (i < new_size) {
  //  AllocFrame(GetPage(start_address + i, true, kernel_directory), supervisor,
  //             readonly);
  //  i += 0x1000;
  //}
  //end_address = start_address + new_size;
}

uint32_t Heap::Contract(uint32_t new_size) {
  PANIC("Need to contract");
  return end_address - start_address;
  //if (new_size >= end_address - start_address)
  //  PANIC("Bad size");

  //if (new_size & 0x1000) {
  //  new_size &= 0x1000;
  //  new_size += 0x1000;
  //}

  //if (new_size < HEAP_MIN_SIZE)
  //  new_size = HEAP_MIN_SIZE;
  //uint32_t old_size = end_address - start_address;
  //uint32_t i = old_size - 0x1000;
  //while (new_size < i) {
  //  FreeFrame(GetPage(start_address + i, false, kernel_directory));
  //  i -= 0x1000;
  //}
  //end_address = start_address + new_size;
  //return new_size;
}

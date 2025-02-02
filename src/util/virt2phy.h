// Credits: DPDK

#pragma once

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include "common.h"

namespace erpc {

/**
 * @brief A class to translate any mapped virtual address in the current process
 * to its physical address.
 *
 * Requires root access.
 */
class Virt2Phy {
  static constexpr size_t kPfnMaskSize = 8;

 public:
  Virt2Phy() {
    fd_ = open("/proc/self/pagemap", O_RDONLY);
    if (fd_ < 0) {
      printf("%s(): cannot open /proc/self/pagemap\n", strerror(errno));
      exit(-1);
    }

    page_size_ = static_cast<size_t>(getpagesize());  // Standard page size
  }

  ~Virt2Phy() { close(fd_); }

  /**
   * @brief Return the physical address of this virtual address
   * @return The physical address on success, zero on failure
   */
  uint64_t translate(const void *virtaddr) {
    auto virt_pfn = static_cast<unsigned long>(
        reinterpret_cast<uint64_t>(virtaddr) / page_size_);
    size_t offset = sizeof(uint64_t) * virt_pfn;

    uint64_t page;
    int ret = pread(fd_, &page, kPfnMaskSize, static_cast<long>(offset));

    if (ret < 0) {
      fprintf(stderr, "cannot read /proc/self/pagemap: %s\n", strerror(errno));
      return 0;
    } else if (ret != static_cast<int>(kPfnMaskSize)) {
      fprintf(stderr,
              "read %d bytes from /proc/self/pagemap but expected %zu:\n", ret,
              kPfnMaskSize);
      return 0;
    }

    // The pfn (page frame number) are bits 0-54 (see pagemap.txt in linux
    // Documentation)
    if ((page & 0x7fffffffffffffULL) == 0) return 0;

    uint64_t physaddr = ((page & 0x7fffffffffffffULL) * page_size_) +
                        (reinterpret_cast<uint64_t>(virtaddr) % page_size_);

    return physaddr;
  }

 private:
  int fd_;
  size_t page_size_;
};

class HugepageCachingVirt2Phy {
 public:
  uint64_t translate(void *_va) {
    uint64_t va = reinterpret_cast<uint64_t>(_va);
    uint64_t va_2_mb = (va & ~(MB(2) - 1));

    auto result = v2p_cache_.find(va_2_mb);
    if (likely(result != v2p_cache_.end())) {
      return result->second + (va % MB(2));
    }

    // Here, we have a cache miss
    uint64_t phy_addr = v2p_.translate(reinterpret_cast<void *>(va_2_mb));
    v2p_cache_.emplace(va_2_mb, phy_addr);

    return phy_addr + (va % MB(2));
  }

 private:
  Virt2Phy v2p_;
  std::unordered_map<uint64_t, uint64_t> v2p_cache_;
};

}  // namespace erpc

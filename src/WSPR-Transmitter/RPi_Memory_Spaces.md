# Memory Spaces in the Raspberry Pi

This document explains the three distinct address spaces used in the Raspberry Pi and describes how these address spaces are leveraged when accessing peripherals and using the DMA engine.

---

## 1. Address Spaces in the Raspberry Pi

The Raspberry Pi uses **three** different address spaces:

### 1.1. Physical Addresses

- **Definition**: These are the actual address locations in the RAM. They correspond directly to offsets into `/dev/mem`.
- **Peripheral Mapping**:
  - **RPi1**: Peripherals are located at physical address `0x2000000`.
  - **RPi2/3**: Peripherals are located at physical address `0x3F000000`.

### 1.2. Virtual Addresses

- **Definition**: These are the addresses that programs see and use to read and write data.
- **User Space**:
  - Available from `0x00000000` to `0xBFFFFFFF`.
- **Kernel Space**:
  - Addresses starting at `0xC0000000` are reserved for the kernel.
- **Peripheral Access**:
  - Peripherals appear at virtual address `0xF2000000`, but this range is accessible only by the kernel. Even running as 'root' does not necessarily grant a user space application access to these addresses.

### 1.3. Bus Addresses

- **Definition**: A separate (often virtual) address space that maps onto physical memory.
- **Peripheral Mapping**:
  - In bus address space, peripherals start at `0x7E000000`.
- **DRAM Mapping**:
  - The DRAM can be accessed in four different ways in bus address space:
    1. **`0x00000000`**: L1 and L2 cached alias.
    2. **`0x40000000`**: L2 cache coherent (non-allocating).
    3. **`0x80000000`**: L2 cache only.
    4. **`0xC0000000`**: Direct, uncached access.

---

## 2. Accessing Peripherals from User Space

- **Method**: The `mmap` system call is used to map sections of `/dev/mem` (which exposes the physical memory) to a chosen virtual address.
- **Example**:
  - For accessing the GPIO registers, the physical addresses corresponding to the GPIO peripherals are mapped to a virtual address space chosen by the kernel.
  - After mapping, any write to the virtual address actually updates the corresponding physical GPIO address.

---

## 3. Accessing RAM via the DMA Engine

When using the DMA engine to move data between memory areas, a few additional steps are required:

1. **Determine Physical Addresses**:
   Find the physical addresses corresponding to the virtual memory locations where data resides.

2. **Convert to Bus Addresses**:
   Translate these physical addresses into bus addresses. The DMA engine operates using these bus addresses (typically those starting with `0xC` for direct, uncached access).

3. **Program the DMA Engine**:
   With the proper bus addresses, configure the DMA engine to perform memory transfers.

---

## 4. Peripheral Addressing in Broadcom Documentation

- The Broadcom documentation typically describes peripheral addresses in terms of bus addresses.
- Programs must perform calculations and set up data structures to map these bus addresses to virtual addresses, allowing the kernel or user space applications (with proper permissions) to interact with the hardware.

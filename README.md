# Minimalist Wacom CTL-472 Raw Data Driver

A high-performance, minimalist C-based driver for the **Wacom One (CTL-472)**. This project is designed to achieve the **lowest possible input latency** by reading raw HID packets directly from the hardware, bypassing Windows Ink and standard OS cursor smoothing.

---

## 🚀 Key Features

* **Near-Zero Latency**: Accesses the `0xff0d` Vendor-Specific interface for raw, unfiltered hardware data.
* **High-Speed Movement**: Utilizes the Windows `SendInput` API for the fastest cursor updates available in User Mode.
* **External Configuration**: Define your active area in a simple `config.txt` file without recompiling.
* **Anti-Jump Logic**: Built-in validation to prevent the cursor from snapping to (0,0) when the pen leaves the tablet's proximity.
* **Lightweight**: Minimal CPU footprint (near 0%) using synchronous blocking HID reads.

---

## 🛠️ Prerequisites

* **OS**: Windows 10 / 11
* **Library**: [HIDAPI](https://github.com/libusb/hidapi)
* **Compiler**: GCC (via MinGW-w64) or MSVC

---

## 📦 Compilation & Setup

1.  **Install HIDAPI**: Ensure you have the HIDAPI headers and library files.
2.  **Compile**: Use the following command to link necessary libraries:
    ```bash
    gcc main.c -o wacom_driver.exe -lhidapi -luser32
    ```
3.  **Config**: Create a `config.txt` in the same folder as the `.exe`.

---

## ⚙️ Configuration (`config.txt`)

The driver maps your specified area to the full resolution of your monitor. Edit the values to match your preferred tablet area (raw units):

```text
MAX_X=6800
MAX_Y=5400
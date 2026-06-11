# DDS Performance Benchmark (Bachelor's Thesis)

This repository contains the source code for an automated benchmark designed to empirically evaluate the performance, scalability, and network resilience of different implementations of the Data Distribution Service (DDS) standard.

This project was developed as a Bachelor's Thesis (Trabajo Fin de Grado) in Computer Engineering.

**Author:** Hugo Blázquez Esquinas  
**Tutor:** Francisco Javier García Blas  
**Institution:** Universidad Carlos III de Madrid (UC3M) 

## Project Structure

The repository is modularly divided into execution nodes (C/C++) and orchestration/monitoring scripts (Python):

* `common/`: Python scripts designed for local loopback evaluation.
* `cluster/`: Distributed scripts for advanced network fragmentation and multimedia streaming tests in a physical cluster environment.
  
## Prerequisites and Dependencies

**IMPORTANT:** In order to successfully compile and link the C++ communication nodes, **you must have the following three DDS implementations installed** and properly configured in your system's environment variables:

1. **[eProsima Fast DDS](https://fast-dds.docs.eprosima.com/)** (v2.x)
2. **[Eclipse CycloneDDS](https://cyclonedds.io/)**
3. **[OpenDDS](https://opendds.org/)**

### Other requirements:
* **CMake** (>= 3.10)
* **C++ Compiler** (GCC/Clang with C++11 or higher support)
* **Python 3.8+** (Required for the orchestration scripts)
* Python packages: `psutil` (for hardware monitoring)

## 🛠️ Build Instructions

The project uses CMake for cross-platform compilation. To build the C++ nodes for all the DDS implementations, run the following commands from the root of the repository:

```bash
mkdir build
cd build
cmake ..
make

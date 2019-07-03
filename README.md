# ICA Plugin

This plugin for the [Open Ephys GUI](http://www.open-ephys.org/gui) allows you to decompose incoming data using ICA, and then exclude specified components. This can be used to reduce common noise, for instance - for more information, see [Whitmore & Lin, "Unmasking local activity within local field potentials (LFPs) by removing distal electrical signals using independent component analysis"](https://linkinghub.elsevier.com/retrieve/pii/S1053811916001415).

<img src="ica_editor.png" width="220" />

## Requirements

Depends on the [Eigen](http://eigen.tuxfamily.org/index.php?title=Main_Page) header library, which is included at the top level as a submodule. After cloning, you must also initialize the submodule:

```
cd eigen
git submodule init
git submodule update
```

The plugin depends on a program called [BINICA](https://sccn.ucsd.edu/wiki/Binica). Since the compilation process seems complex and the precompiled versions are well-tested, just the compiled binaries for each platform are currently included under `/binica`. The following platforms are supported:

* Windows Intel x64
* Linux Intel 32-bit
  * If you are on a 64-bit architecture, you might have to install 32-bit C libraries to run the executable. For instance, on Ubuntu 18.04, I had to do so as follows:
  
    ```
    sudo dpkg --add-architecture i386
    sudo apt-get update
    sudo apt-get install libc6:i386
    ```
* Mac OS Intel 32- or 64-bit
* Mac OS PowerPC 32- or 64-bit

## Installation

First, you must download and build the Open Ephys GUI source - the library it exports is required to build plugins for it. Then, clone this repository in a neighboring folder to the `plugin-GUI` repository and follow these instructions: [Create the build files through CMake](https://open-ephys.atlassian.net/wiki/spaces/OEW/pages/1259110401/Plugin+CMake+Builds).

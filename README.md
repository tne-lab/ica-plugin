# ICA Plugin

This plugin for the [Open Ephys GUI](http://www.open-ephys.org/gui) allows you to decompose incoming data using ICA, and then exclude specified components. This can be used to reduce common noise, for instance - for more information, see [Whitmore & Lin, "Unmasking local activity within local field potentials (LFPs) by removing distal electrical signals using independent component analysis"](https://linkinghub.elsevier.com/retrieve/pii/S1053811916001415).

**Use this branch if you are building the main GUI and plugins without using CMake. If you have updated your plugin-GUI repository to use CMake for all building (on the development branch as of writing), use the *cmake-gui* branch instead.**

<img src="ica_editor.png" width="350" /> <img src="ica_canvas.png" width="467"/>

## Requirements

### Eigen

This plugin depends on the [Eigen](http://eigen.tuxfamily.org/index.php?title=Main_Page) library for matrix manipulation (version 3.3.x). If you are on Linux or macOS, Eigen should be available through your usual package repository (e.g., the Ubuntu package is called `libeigen3-dev`).

Windows users have 2 options:

* If you already have a compatible version of Eigen somewhere on your computer, you just need to set the environment variable `Eigen3_DIR` to the path to your Eigen root directory. (You can also set this when calling CMake with the option `-DEigen_Dir="C:/path/to/eigen"`.) This will allow CMake to find and link to Eigen. If you set an environment variable, be sure to completely restart the shell process from which you are calling `cmake`.

* Otherwise, you can clone Eigen as a submodule of this repository. This is already set up, but the submodule does not get downloaded by default when you clone `ica-plugin`. To do so, use the following commands:

```
git submodule init
git submodule update
```

### BINICA

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

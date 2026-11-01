# FUN OFDM #

[![CI](https://github.com/bmorgan5/fun_ofdm/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/bmorgan5/fun_ofdm/actions/workflows/ci.yml)

This project is an 802.11a OFDM PHY layer implementation written in C++ for use with Ettus USRPs. It contains both transmitter and receiver blocks and were tested with USRP N210s + XCVR 2450 daughterboards. These blocks are provided separately so that they can be used individually. They can also be easily combined into a single transceiver, but that is left up to the user because how they interact with each other should be controlled by the MAC layer which is outside the scope of this project.

The [Official FUN OFDM API can be found here!](http://www.ee.washington.edu/research/funlab/fun_ofdm/index.html)

For how the PHY works, with diagrams of the frame and the transmit and receive pipelines, see [src/phy/README.md](src/phy/README.md).

# Getting Set Up #

## Hardware Requirements ##

__1. USRP N2XX__

This project was built for and tested with USRP N210s. It may be updated in the future to work with the B200/210's as well, but for now if you want to use B210s you will need to update the USRP class appropriately.

__2. Gigabit Ethernet Port__

Each USRP requires its own gigabit ethernet port.

__3. Second Generation Intel i5/i7 processor (or equivalent) and 8 GB RAM__

The transmitter is not very computationally complex. However, the receiver runs about 6 threads with about each one performing O(N) complex multiplications each. Therefore, to run the receiver (either by itself or as part of a transceiver) it is recommended that you have at least a 2nd generation Intel i5/i7 or equivalent and at least 8 GB of RAM.


## Dev Container ##

The easiest way to get a working build environment is the included dev container, which has every dependency (plus Claude Code) preinstalled.

You'll need Docker and VS Code with the [Dev Containers](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) extension. Open the repo in VS Code and run "Dev Containers: Reopen in Container".

Without VS Code, use the [Dev Containers CLI](https://github.com/devcontainers/cli) (requires Node 18+):

~~~
npm install -g @devcontainers/cli
devcontainer up --workspace-folder .
devcontainer exec --workspace-folder . bash
~~~

Note: the container is x86-64 because the code uses SSE intrinsics.

## Manual Install ##

The build only needs [bazelisk](https://github.com/bazelbuild/bazelisk) installed as `bazel`; Bazel downloads the compiler and the libraries to build against (see below). Running the binaries needs Ubuntu 26.04 with UHD installed:

~~~
sudo apt-get install libuhd4.9.0
~~~

## Build ##

The project builds with [Bazel](https://bazel.build), using clang from [toolchains_llvm](https://github.com/bazel-contrib/toolchains_llvm):

~~~
bazel build fun-ofdm
~~~

The build produces a single binary, `bazel-bin/src/fun-ofdm`, which runs the transmitter (`fun-ofdm tx`), the receiver (`fun-ofdm rx`) or a software simulation (`fun-ofdm sim`). Builds are optimized (`-c opt`) by default; pass `-c dbg` for a debug build. To build and run it in one step:

~~~
bazel run fun-ofdm -- sim
~~~

These short target names work from the workspace root; from anywhere else, use `//:fun-ofdm` (and `//:install` below).

The binary links everything statically except glibc, libstdc++ (and its libgcc_s) and libuhd, so it runs on any Ubuntu 26.04 machine with `libuhd` installed.

`bazel-bin` is a symlink into Bazel's cache inside the container, so to get the binary out (e.g. onto the host, to copy to a target), install it into the workspace:

~~~
bazel run install
~~~

This copies it to `out/<platform>/bin/` (`out/linux_x86_64/bin/` by default), so builds for different architectures don't overwrite each other.

### Sysroots and cross compiling ###

Every build is a cross compile against a sysroot for the target platform, even for the host's architecture, so the compiler never sees the host's `/usr`. The sysroots are made of Ubuntu 26.04 packages (`SYSROOT_PACKAGES` in `MODULE.bazel`, and their dependencies), which Bazel downloads from a dated snapshot of the Ubuntu archive (`UBUNTU_SNAPSHOT`) and unpacks itself, without installing anything. The resolved package list, with each package's checksum, is recorded in `MODULE.bazel.lock`, so every checkout builds against the same files. To pick up package updates, move `UBUNTU_SNAPSHOT` forward; the next build resolves the packages again and updates the lockfile.

Only x86-64 (`--config=linux_x86_64`, the default) is enabled for now. arm64 support is stubbed out under `TODO(arm64)` comments in `MODULE.bazel`, `.bazelrc` and `third_party/BUILD.bazel`, and needs the SSE code to be ported first.

### Documentation (Optional) ###

The project uses Doxygen to generate HTML API documentation, with this README as the main page. Bazel downloads Doxygen itself, so there's nothing to install:

~~~
bazel build //docs
~~~

The documentation ends up in `bazel-bin/docs/html/`, which is inside the container. To view it on the host, build it and copy it into the workspace instead:

~~~
bazel run //docs:install
~~~

Then open `out/docs/html/index.html`. The docs aren't built by `bazel build //...`.

## Formatting ##

C++ follows Google's style, formatted with clang-format, and the Bazel files are formatted with buildifier. Bazel downloads both, so everyone gets the same output:

~~~
bazel run format         # Format every file
bazel run format.check   # Change nothing; fail if any file needs formatting
~~~

Once per clone, enable the pre-commit hook, which runs `format.check` on the staged files, and have `git blame` skip the commit that reformatted the tree. The dev container does this when it's created; otherwise, run:

~~~
git config core.hooksPath tools/githooks
git config blame.ignoreRevsFile .git-blame-ignore-revs
~~~

VS Code formats C++ on save (see `.vscode/settings.json`), with the C/C++ extension's clang-format and the repo's `.clang-format`. The dev container installs the extension. Its clang-format can be a different version from Bazel's, so if the two disagree, `bazel run format` wins.

# Testing #

## Simulation ##

The easiest way to test that everything built correctly is to run *fun-ofdm sim* (`bazel run fun-ofdm -- sim`) as this does not require any USRPs to be connected. The source code for this mode is `run_sim()` in src/main.cpp. If everything compiled correctly you should see an output that looks something like:


> [path to fun_ofdm]/fun_ofdm $ sudo bazel-bin/src/fun-ofdm sim
> I'm a little tea pot, short and stout.....here is my handle.....blah blah blah.....this rhyme sucks!I'm a little tea pot, short and stout.....here is my handle.....blah blah blah.....this rhyme sucks!I'm a little tea pot, short and stout.....here is my handle.....blah blah blah.....this rhyme sucks!
>
> ...
>
> ...
>
> I'm a little tea pot, short and stout.....here is my handle.....blah blah blah.....this rhyme sucks!I'm a little tea pot, short and stout.....here is my handle.....blah blah blah.....this rhyme sucks!I'm a little tea pot, short and stout.....here is my handle.....blah blah blah.....this rhyme sucks!
>
> Received 100 packets
>
> Time elapsed: 1500.481000

## Test Tx ##

To test that the transmitter is working you can run *fun-ofdm tx*. This test requires a USRP so be sure to have one plugged connected properly. (To test if your computer can see the USRP you can use 'uhd_find_devices'). The source code for this is `run_tx()` in src/main.cpp. If everything goes as expected you should see soemthing like:

> [path to download dir]/fun_ofdm $ sudo bazel-bin/src/fun-ofdm tx
>
> linux; GNU C++ version 4.8.1; Boost_105300; UHD_003.007.001-72-g383061d8
>
> Testing transmit chain...
>
> -- Opening a USRP2/N-Series device...
>
> -- Current recv frame size: 1472 bytes
>
> -- Current send frame size: 1472 bytes
>
> Sending burst 1 of 20 at 1/2 BPSK
>
> Sending burst 2 of 20 at 1/2 BPSK
>
> Sending burst 3 of 20 at 1/2 BPSK
>
> Sending burst 4 of 20 at 1/2 BPSK
>
> Sending burst 5 of 20 at 1/2 BPSK
>
> ...
>
> ...
>
> Sending packet 995 of 1000 at 1/2 BPSK
>
> Sending packet 996 of 1000 at 1/2 BPSK
>
> Sending packet 997 of 1000 at 1/2 BPSK
>
> Sending packet 998 of 1000 at 1/2 BPSK
>
> Sending packet 999 of 1000 at 1/2 BPSK
>
> Sending packet 1000 of 1000 at 1/2 BPSK

## Test Rx ##

Testing the receiver is a bit more complicated. This test requires at least one USRP for the receiver, but you will probably want to have a second USRP so that you can transmit packets for the receiver to receive as well. To do this,
first run *fun-ofdm rx*. If you are on a clear channel and don't have anything transmitting you will probably see something like this:

> [path to download dir]/fun_ofdm $ sudo bazel-bin/src/fun-ofdm rx
>
> linux; GNU C++ version 4.8.1; Boost_105300; UHD_003.007.001-72-g383061d8
>
> Testing receive chain...
>
> Instantiating the usrp.
>
> -- Opening a USRP2/N-Series device...
>
> -- Current recv frame size: 1472 bytes
>
> -- Current send frame size: 1472 bytes
>
> -- Detecting internal GPSDO.... No GPSDO found
>
> -- not found
>
> Invalid CRC (length 709)
>
> Invalid CRC (length 3050)
>

Don't worry about the 'Invalid CRC (length 709)' as this just indicates a false alarm that was successfully detected and dropped when the CRC failed in the frame decoder block.

Once you have the receiver up and running you can then run *fun-ofdm tx* to transmit some packets for the receiver to receive. If everything works as expected you should see something like this:

> [path to download dir]/fun_ofdm $ sudo bazel-bin/src/fun-ofdm rx
>
> linux; GNU C++ version 4.8.1; Boost_105300; UHD_003.007.001-72-g383061d8
>
> Testing receive chain...
>
> Instantiating the usrp.
>
> -- Opening a USRP2/N-Series device...
>
> -- Current recv frame size: 1472 bytes
>
> -- Current send frame size: 1472 bytes
>
> Invalid CRC (length 1775)
>
> Received 1 packets at 23:33:55.636086
>
> Received 2 packets at 23:33:55.639852
>
> Received 3 packets at 23:33:55.643764
>
> Received 4 packets at 23:33:55.647949
>
> Received 5 packets at 23:33:55.652317
>
> Received 6 packets at 23:33:55.656672
>
> Received 7 packets at 23:33:55.662527
>
> Received 8 packets at 23:33:55.666724
>
> Received 9 packets at 23:33:55.670681
>
> Received 10 packets at 23:33:55.673931
>
> ...
>
> ...
>
> Received 995 packets at 08:23:39.734422
>
> Received 996 packets at 08:23:39.744281
>
> Received 997 packets at 08:23:39.754190
>
> Received 998 packets at 08:23:39.764118
>
> Received 999 packets at 08:23:39.773871
>
> Received 1000 packets at 08:23:39.783544

Depending on the PHY Rate used at the transmitter and if you are lucky (as I was when I ran this test) you will receive all transmitted packets. However, as long as you receive anywhere >90% of transmitted packets you can safely assume that you have everything set up correctly.

# Usage #

This project was developed as a library for furthering wireless communications research in the Fundamentals of Networking lab (FUNLAB) at the University of Washington and is released under GPL V2 for anybody to use/modify however they like.

The full API is available on the funlab website: [fun_ofdm API](http://www.ee.washington.edu/research/funlab/fun_ofdm/index.html).

## Parameters ##

The main parameters used for the transmitter / receiver are as follows:

* freq        - Center frequence [default=5.72e9 (5.72 GHz)]
* sample_rate - AD/DA sampling frequency (also corresponds to bandwidth) [default=5e6 (5 MHz)]
* tx_gain     - Output amplifier gain (0-35 on USRP N210) [default=20]
* rx_gain     - Input amplifier gain  (0-35 on USRP N210) [default=20]
* tx_amp 		  - Scale factor for transmit samples. Used to give finer control over output power. [default=1]
* device_addr - IP Address of USRP as a string. An empty string will automatically find an eligable USRP [default=""]

* phy_rate 	  - The modulation and coding rate used when building the phy frames [default=1_2_BPSK (BPSK w/ Rate R=1/2 Convolutional Code)]

## Transmitter ##

Building the transmitter and sending packets is very easy. The following code snippet builds a transmitter object with the default USRP parameters and sends a single packet.

~~~
...

usrp_params params = usrp_params();
transmitter tx = transmitter(params);

std::string s = "Hello World";
std::vector<unsigned char> data = std::vector<unsigned char>(12);
memcpy(&data[0], &s[0], 12);
tx.send_packet(data);

...

~~~

## Receiver ##

Similarly, building the receiver and receiving packets is just as. The following code snippet builds a receiver object with the default USRP parameters and passes a function pointer to the callback function aptly named 'callback'.

~~~
...

usrp_params params = usrp_params();
receiver rx = receiver(&callback, params);

while(1) sleep(1); //Let the main thread spin while the receive threads receives packets

...
~~~

## Simple Transciever ##

Putting the above two examples together we can make a very simple transceiver using the default USRP parameters that receives for 4 seconds then transmits a single packet and repeats. *Note: the callback function is only called if the receiver actually successfully receives a packet. In this case the contents of the packet are simply printed to standard out (i.e. the terminal). If the receiver doesn't receive anything then essentially nothing happens.

~~~
int main()
{

    usrp_params params = usrp_params();
    transmitter tx = transmitter(params);
    receiver rx = receiver(&callback, params);

    std::string s = "Hello World";
    std::vector<unsigned char> data = std::vector<unsigned char>(12);
    memcpy(&data[0], &s[0], 12);
    tx.send_packet(data);

    while(1)
    {
        sleep(4);
        rx.pause();
        tx.send_packet(data);
        cout << "Sending \"Hello World\" " << std::endl;
        rx.resume();
    }

    return 0;
}

void callback(std::vector<std::vector<unsigned char> > payloads)
{
    for(int i = 0; i < payloads.size(); i++)
    {
        std::cout << "Received a packet" << std::endl;
    }

}
~~~

This example only needs one USRP to be connected since the transmitter and receiver as the code is never trying to transmit and receive at the same time.


# Troubleshooting #

+ Can you see your USRP with 'uhd_find_device'?
+ Did you run fun-ofdm as root (i.e. 'sudo')?
+ Have you tried different gain / tx_amp settings?
+ Are both the transmitter and receiver using the same center freq? sample rate?
+ Are compiler optimizations turned on (they are by default; see .bazelrc)?
+ If you see 'D's printed to the screen that might indicate hardware problems
	+ Could be your ethernet card - see the sysconf changes recommended by Ettus
	+ Could be your processor is too slow/too busy
	+ Could be not enough Ram available

# Contact #

For help troubleshooting please make sure to include the following:

+ Operating System (Ubuntu, Linx Mint, etc.) and version
+ USRP & Daughtercard (USRP N210 & XCVR 2450)
+ What parameters you are using (freq, sample rate, etc)
+ What you are trying to do/what is not working
+ Any other useful information

Any helpful feedback (i.e. anything that isn't spam or trolling) is much appreciate!


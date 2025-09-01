
# Features
- Concurrent recording from 2 frame and 2 EBV cameras (tested)
- Allows setting the biases for the EBV cameras (individually)
- high-performing concurrent data recording without loosing frames / data (facilitated natively by OpenEB); For frame cameras this is facilitated by using threading, buffering the recorded frames to memory and using diskWriterWorker to write to disk to prevent i/o bound dropping of frames. 


# install dependencies

sudo apt install libgtk2.0-dev pkg-config


### Install IDS_peak
# TODO, following standard instructions
### IDS peak
# For the color cameras install ids peak:
# download from https://en.ids-imaging.com/download-details/1008717.html?os=linux&version=&bus=64&floatcalc=
sudo apt-get install ./ids-peak_2.16.0.0-457_amd64.deb

# launch with 
ids_peak_cockpit



### OpenEB (Metavision)
```
mkdir external
cd external
git clone https://github.com/prophesee-ai/openeb.git --branch 5.1.1
# TODO continue here (but for EBV, python worked good enough)

sudo apt update
sudo apt -y install apt-utils build-essential software-properties-common wget unzip curl git cmake
sudo apt -y install libopencv-dev libboost-all-dev libusb-1.0-0-dev libprotobuf-dev protobuf-compiler
sudo apt -y install libhdf5-dev hdf5-tools libglew-dev libglfw3-dev libcanberra-gtk-module ffmpeg 
sudo apt -y install libgtest-dev libgmock-dev # Optional for tests

# Python Bindings
# This is modified from the original instructions, so we only install locally. In particular, we use 
- conda to get python3.12
- create a venv in the project directory in which everything is installed (metavision python bindings)
- install pybind only locally to this project
- install OpenEB only locally to this project
- install OpenEB python bindings only in the venv


# I install python 3.12 using conda
conda create -n python3.12 python==3.12
conda activate python3.12


python3 -m venv /cshome/share/ott/EBV_stuff/ebv_frame_recording/external/py3venv --system-site-packages
/cshome/share/ott/EBV_stuff/ebv_frame_recording/external/py3venv/bin/python -m pip install pip --upgrade
/cshome/share/ott/EBV_stuff/ebv_frame_recording/external/py3venv/bin/python -m pip install -r ./openeb/utils/python/requirements_openeb.txt

source /cshome/share/ott/EBV_stuff/ebv_frame_recording/external/py3venv/bin/activate
# Guide says pybind11 has to be installed manually, but this here works:
# pip install pybind11==2.11.0
# Install ids_peak
pip install --upgrade pip setuptools wheel
pip install ids_peak ipykernel opencv-python matplotlib


wget https://github.com/pybind/pybind11/archive/v2.11.0.zip
unzip v2.11.0.zip
cd pybind11-2.11.0/
mkdir build && cd build
cmake .. -DPYBIND11_TEST=OFF -DCMAKE_INSTALL_PREFIX=/cshome/share/ott/EBV_stuff/ebv_frame_recording/external/py3venv
cmake --build .
cmake --install .
# sudo cmake --build . --target install
cd ../..

# Fix required to make build work (to find libraries)
export LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu:$LIBRARY_PATH
export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH

cd openeb
mkdir build && cd build
cmake .. -DBUILD_TESTING=OFF
cmake --build . --config Release -- -j 16

# Working from the `build` folder -> makes python imports available
source utils/scripts/setup_env.sh

# Installing the camera plugin
# (Now it is imported through `MV_HAL_PLUGIN_PATH`; it could also be put into `/external/openeb/build/lib/metavision/hal/plugins` for automatic import, but running setup script is required anyways)
cd ..
cp ~/Downloads/ueye-evs_5.0.0.3_amd64.tar.gz ./
tar -xzf ueye-evs_5.0.0.3_amd64.tar.gz
cd ueye-evs_5.0.0.3_amd64/lib/ids/ueye_evs/scripts/
chmod +x add_ueye_evs_users_group.sh
chmod +x add_udev_rules.sh
chmod +x add_ids_mv_hal_plugin_path.sh
sudo sh setup.sh

# Deb install (does not work due to unmet metavision deb install dependencies)
https://en.ids-imaging.com/download-details/1011378.html?os=linux&version=&bus=64&floatcalc=
sudo apt-get install ./ueye-evs_4.6.2.2_amd64.deb



# # TODO include camera plugin
# # Put it inside /external/openeb/build/lib/metavision/hal/plugins
# # This location gets searched by default
# # Download `Plugin 5.0.0.3 for uEye EVS cameras / Metavision SDK 5.0.0 + 5.1.0 for Ubuntu 24.04 - archive file` from https://en.ids-imaging.com/download-details/1011378.html?os=linux&version=&bus=64&floatcalc=
# cd ./lib/metavision/hal/plugins
# cp ~/Downloads/ueye-evs_5.0.0.3_amd64.tar.gz ./
# tar -xzf ueye-evs_5.0.0.3_amd64.tar.gz
# rm ueye-evs_5.0.0.3_amd64.tar.gz


# copy plugins manually (-> Works, probably even without installation if udev rules are manually copied):
sudo cp ueye-evs_5.0.0.3_amd64/lib/ids/ueye_evs/scripts/99-ueye_evs.rules /etc/udev/rules.d/
cp ueye-evs_5.0.0.3_amd64/lib/ids/ueye_evs/hal/plugins/* openeb/build/lib/metavision/hal/plugins/


```

# check if devices can be discovered:
from metavision_hal import DeviceDiscovery
DeviceDiscovery.list()



### OpenCV

```
cd external
git clone https://github.com/opencv/opencv.git --branch 4.12.0
cd opencv
mkdir build
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja
```


### CLI11
cd external
mkdir CLI11
cd CLI11
wget https://github.com/CLIUtils/CLI11/releases/download/v2.5.0/CLI11.hpp



# Qt6
sudo apt install qt6-base-dev qt6-base-dev-tools qt6-tools-dev qt6-tools-dev-tools

# Build
```
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja

cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .. && ninja && bin/ebv_frame_recording


cd /cshome/share/ott/EBV_stuff/ebv_frame_recording/build && ninja

```




# Testing (TODO only works with connected EBV cameras..., should be automated)
bin/ebv_frame_recording -s 4108900147 4108900356 --bias_diff_on 1 10 --bias_diff_off 2 20 --bias_fo 3 30 --bias_hpf 4 40 --bias_refr 0 0 # -> should load the EV cameras with the bias settings and do a recording
bin/ebv_frame_recording -s 4108900147 # -> should error out due to no second event camera specified
bin/ebv_frame_recording -s 4108900148 # -> should throw camera not found error



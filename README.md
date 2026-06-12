# Lindora

An unofficial site-specific browser wrapper for the Pandora web service. Not affiliated with or endorsed by Pandora. Contains taskbar controls.

# Disclaimer

Lindora is an independent, non-official project. It is not affiliated with, endorsed by, or connected to Pandora. All trademarks are the property of their respective owners.

# Arch-Based

```
git clone https://github.com/Logawinner/Lindora && cd Lindora && makepkg -si
```
or

```
sudo pacman -S lindora
```
```
yay -S lindora
```
```
paru -S lindora
```
# Debian-Based 

```
echo "deb https://Logawinner.github.io/lindora-apt stable main" | sudo tee /etc/apt/sources.list.d/lindora.list

sudo apt update
sudo apt install lindora
```

# Other

```
# 1. Download the code
git clone https://github.com/Logawinner/Lindora && cd Lindora

# 2. Create a build directory and enter it
mkdir build && cd build

# 3. Configure the project
cmake ..

# 4. Compile the code
make

# 5. Install the files to the system
sudo make install
```

# Features

* On systems where applicable the app should reveal a system tray that stays open when the main window is close.
* Right clicking the system tray reveals a system tray menu.
* Clicking the system tray opens or closes the main window.
* Hovering shows which song is current playing.
* Dark mode color scheme

# Contributing

* Every PR will be reviewed before merging
* Please use clean code
* Please document your changes
* Test your code before making a PR
* If your PR breaks the code on repo it will get rejected

# Issues

* Provide the version, distro, DE, and the problem in the issues section on Github.
* Helping with issues is appreciated.

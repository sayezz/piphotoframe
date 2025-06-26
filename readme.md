# Mount network folder
sudo nano /etc/fstab
add the following line
//192.168.xx.xx/folder /mnt/path cifs username=xxx,password=xxx,vers=3.0

To mount all:
sudo mount -a

# Configure as service
In your home directory ("cd ~") create and open:
nano .config/systemd/user/piphotoframe.service

In the editor add the following:

[Unit]
Description=Start photo frame after login
After=graphical-session.target

[Service]
ExecStart=sudo /home/pi/piphotoframe/main
WorkingDirectory=/home/pi/piphotoframe/
Restart=always
Environment=DISPLAY=:0
Environment=XAUTHORITY=/home/pi/.Xauthority

[Install]
WantedBy=default.target


Configer linger:
sudo loginctl enable-linger pi

Enable the service:
systemctl --user enable displayImage.service

Service gets started as user because I need to make sure that network, and desktop UI is finished loading.
# start service
systemctl --user start piphotoframe.service
# stop service
systemctl --user stop piphotoframe.service

# Dependencys
sudo apt install nlohmann-json-dev
sudo apt install libopencv-dev
sudo apt install libx11-dev
sudo apt install libexiv2-dev
sudo apt install build-essential gdb

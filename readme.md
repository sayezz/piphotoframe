# Mount network folder
sudo nano /etc/fstab
add the following line
//192.168.17.29/homes /mnt/paulNAS cifs username=Denis,password=f7Cq7qs5k40wlyfA,vers=3.0

To mount all:
sudo mount -a

# Configure as service
In your home directory ("cd ~") create and open:
nano .config/systemd/user/displayImage.service

In the editor add the following:

[Unit]
Description=Start DisplayImage after login
After=graphical-session.target

[Service]
ExecStart=sudo /home/pi/showimg/main
WorkingDirectory=/home/pi/showimg/
Restart=always
Environment=DISPLAY=:0
Environment=XAUTHORITY=/home/pi/.Xauthority

[Install]
WantedBy=default.target


Service gets started as user because I need to make sure that network, and desktop UI is finished loading.
# start service
systemctl --user start displayImage.service
# stop service
systemctl --user stop displayImage.service

# Dependencys
sudo apt install nlohmann-json-dev
sudo apt install libopencv-dev
sudo apt install libx11-dev
sudo apt install libexiv2-dev
sudo apt install build-essential gdb

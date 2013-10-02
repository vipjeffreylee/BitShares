import shutil
import os
import zipfile

def zipdir(path, zip):
    for root, dirs, files in os.walk(path):
        for file in files:
            zip.write(os.path.join(root, file))


if __name__ == '__main__':
    #copy files listed in install_lst.txt to temporary bin dir
    binDir = "bin"
    if os.path.exists(binDir):
        shutil.rmtree(binDir)
    os.mkdir(binDir)
    for line in open("install_list.txt",'r'):
        line=line.strip()
        if line:
            print(line)
            shutil.copy(line,binDir)

    #copy platform file
    platformDir = binDir + '/platforms';
    os.mkdir(platformDir)
    qtdir = os.environ['QTDIR']
    windowsPlatformDll = qtdir + '/qtbase/plugins/platforms/qwindows.dll';
    shutil.copy(windowsPlatformDll,platformDir)
    
    #zip up files in bin dir to keyhotee.zip
    zip = zipfile.ZipFile('keyhotee.zip','w')
    zipdir(binDir, zip)
    zip.close()
        
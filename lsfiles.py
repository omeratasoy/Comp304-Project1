from datetime import datetime
import os

# -*- coding:utf-8 -*-

outpath = './lsfiles_output.txt'
log = './lsfiles_log.txt'

def folderListerA(foldername):
  global outpath
  try:
    for i in os.listdir(foldername): #for each file or folder in the folder
      if not os.path.isfile(os.path.join(foldername, i)): #when there is a file inside the folder
        with open(outpath, 'a') as f:
          f.write(os.path.join(foldername,i))
          f.write('\n')
        folderListerA(os.path.join(foldername,i)) #the recursion
      else:  #when there is not any files in the folder we entered
        with open(outpath, 'a') as f:
          f.write(os.path.join(foldername,i))
          f.write('\n')
  except:
    now = datetime.now() #how is a datetime object
    nowOut = now.strftime("%d/%m/%Y, %H:%M:%S") #now.strftime() converts datetime object to string
    with open(outpath, 'a') as f:
      f.write("Permission denied to " + foldername + '\n')
    with open(log, 'a') as f:
      f.write(nowOut + " PermissionError: " + foldername + '\n')





print("Please enter the full address of the folder.")
print("E.g, '/home/USERNAME/FOLDER'")
foldername = input()
folderListerA(foldername)

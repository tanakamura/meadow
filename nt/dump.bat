@echo off
if exist Meadow.exe del Meadow.exe

.\temacs.exe -batch -l loadup dump

move emacs.exe Meadow.exe

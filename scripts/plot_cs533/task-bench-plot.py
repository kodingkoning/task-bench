import matplotlib.pyplot as plt
import linecache
import math
import numpy as np

implementationName = "openmp"
inputfile = "task-bench-"+implementationName+".dat"
#outputfile="legion-efficiency.eps"

f1 = open("./"+inputfile, "r")

#elapsed,iterations,output,steps,tasks,flops,bytes,width,std,reps,scale_factor,time_per_task,flops_per_second,bytes_per_second,efficiency
title_line = 1
total_line = 20
case = 1 #0: efficiency; 1: TFlops/s; 2: Bytes/s

#Create array
iterations = np.zeros(total_line-title_line)
efficiency = np.zeros(total_line-title_line)
flops_per_second = np.zeros(total_line-title_line)
bytes_per_second = np.zeros(total_line-title_line)

for w in range(title_line):
    result_f1=f1.readline()

for j in range(total_line-title_line):
    result_f1 = f1.readline().split(',')
    #print(result_f1)
    iterations[j] = float(result_f1[1])
    efficiency[j] = float(result_f1[14])
    bytes_per_second[j] = float(result_f1[13])
    flops_per_second[j] = float(result_f1[12])

#figure(figsize=(2.5,2),dpi=300)
if(case==0):
    fig = plt.figure()
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(iterations, efficiency,color='pink',marker='D')
    ax.invert_xaxis()
    ax.set_xscale('log')
    plt.xlabel("Iterations")
    plt.ylabel("Efficiency")
    plt.savefig(implementationName+"-efficiency.eps",format='eps')
#plt.show()

if(case==1):
    fig = plt.figure()
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(iterations, flops_per_second/1.E12,color='pink',marker='D')
    ax.invert_xaxis()
    ax.set_xscale('log')
    plt.xlabel("Iterations")
    plt.ylabel("TFlops/s")
    plt.savefig(implementationName+"-tflop_s.eps",format='eps')

if(case==2):
    fig = plt.figure()
    ax = fig.add_subplot(1, 1, 1)
    ax.plot(iterations, bytes_per_second,color='pink',marker='D')
    ax.invert_xaxis()
    ax.set_xscale('log')
    plt.xlabel("Iterations")
    plt.ylabel("Bytes/s")
    plt.savefig(implementationName+"-bytes_s.eps",format='eps')

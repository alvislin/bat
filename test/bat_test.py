import subprocess
import random
import wave
import math
import struct
import os
import argparse
import sys
import threading
import time
import resource


# Commands example
# bat -D plughw:0,0 -f file.wav -F 250 [-l] [-t]
# bat -D plughw:0,0 -r 44100 -c 2 -s 2 -n 88200 -F 250 [-l] [-t]


# Name of the wav file which is generated by generate_wav_file() function
wav_test_file = r'wav_test_file.wav'

# Path to BAT binary
bat = [r'../src/bat']

# Arguments for BAT 
device = ['-D', 'plughw:0,0']
rate = ['-r', '44100']
channel = ['-c', ' 1']
sample = ['-s', '2']
length = ['-n', '88200']
sine_freq = ['-F', '250']
wav_file = ['-f', wav_test_file]

# Index of test
test_nb = 0;

# Set to true if we expect BAT to fail, false otherwise
expected_fail = False

# Test set for alsa
testset_alsa = {'channel':(1, 2), 'sample size':(1, 2, 4), 'frequency':(8000, 11025, 16000, 22050, 44100, 48000, 88200, 96000, 192000)}
testset_tinyalsa = {'channel':(2,), 'sample size':(2, 4), 'frequency':(44100, 192000)}
testset_hw = {'channel':(2,), 'sample size':(2, 4), 'frequency':(44100, 48000, 96000, 192000)}
testset_hw_fail1 = {'channel':(1,), 'sample size':(2, 4), 'frequency':(44100, 48000, 96000, 192000)}
testset_hw_fail2 = {'channel':(2,), 'sample size':(1,), 'frequency':(44100, 48000, 96000, 192000)}
testset_hw_fail3 = {'channel':(2,), 'sample size':(2, 4), 'frequency':(8000, 22050)}
testset_arg_n = {'channel':(1, 2), 'sample size':(2, 4), 'frequency':(48000, 96000)}

def generate_wav_file(ch, ss, r, f, sf):
    """
    This function generates a wav file
    """
    
    wav_file = wave.open(wav_test_file, 'w')
    wav_file.setparams((ch, ss, r, f, 'NONE', 'not compressed'))
    
    sine = 0
    gain = 0.8
    
    if (ss == 1):
        amplitude = 127
        formatting = 'b'
    elif (ss == 2):
        amplitude = 32760
        formatting = 'h'
    elif (ss == 4):
        amplitude = 2147483648
        formatting = 'i'
         
    values = []     
         
    for i in xrange(f * 2):
        for c in xrange(ch):
            sin_val = float(sine * sf[c])
            sin_val = sin_val / float(r)
            val = gain * amplitude * math.sin(sin_val * 2 * math.pi)
            packed_val = struct.pack(formatting, int(val))           
            values.append(packed_val)
        sine += 1
    
    values_str = ''.join(values)
    wav_file.writeframes(values_str)
    
    wav_file.close()

def generate_command(ch, ss, r, f, sf, fi, extra, dev='-D'):
    """
    This function creates a full command to launch BAT
    """
    
    rate[1] = str(r)
    channel[1] = str(ch)
    sample[1] = str(ss)
    length[1] = str(f)
    if (len(sf) == 1):
        sine_freq[1] = str(sf[0])
    else:
        sine_freq[1] = str(sf[0]) + "," + str(sf[1])
    device[0] = dev
    device[1] = args.device;
    
    command = []
    command.extend(bat)
    command.extend(device)
    
    if (fi == None):
        command.extend(rate)
        command.extend(channel)
        command.extend(sample)
        command.extend(length)
    else:
        command.extend(fi)
    
    command.extend(sine_freq)
    
    command.extend(extra)
    
    return command

def launch_bat(command, verbose, check_time=False):
    FNULL = open(os.devnull, 'w')
        
    try:
        if (check_time == True):
            start_time, start_ressources = time.time(), resource.getrusage(resource.RUSAGE_CHILDREN)

        if (verbose == False):
            subprocess.check_call(command, stdout=FNULL)
        else:
            print '.' * 40
            subprocess.check_call(command)
            print '.' * 40
        
        if (check_time == True):
            end_ressources, end_time = resource.getrusage(resource.RUSAGE_CHILDREN), time.time()
            real_duration = (end_time - start_time) - (end_ressources.ru_utime - start_ressources.ru_utime)
            return real_duration
    except:
        if (expected_fail != True):
            print ' ==> Fail'
            os._exit(1)    
    else:
        if (expected_fail == True):
            print ' ==> Fail'
            os._exit(1)    

def test_arg_n_sine_gen(testset):
    global test_nb
    
    for ch in testset['channel']:
        for s in testset['sample size']:
            for r in testset['frequency']:
                expected_duration = float(random.randint(5, 40)) / 10
                f = str(expected_duration) + 's';
                sf = [random.randint(10, 2 * r / 5) for x in xrange(ch)]
                print '-' * 80
                print 'Test #{}: {} channel(s), {} bytes per sample, sampling rate is {}Hz, sine wave frequency is {}Hz, length is {}s'.format(test_nb, ch, s, r, sf, expected_duration)
                command = generate_command(ch, s, r, f, sf, None, extra=[])
                test_nb += 1
                print '  Calling bat with cmd:', " ".join(command)
                real_duration = launch_bat(command, args. verbose, check_time=True)
                print '  BAT was expected to run ', expected_duration * 1.5 + 0.5, 's and run for ', real_duration, 's'
                
                if ((real_duration > (expected_duration * 1.5 + 0.5) - 0.1) and (real_duration < (expected_duration * 1.5 + 0.5) + 0.1)):
                    print ' ==> Pass'    
                else:
                    print ' ==> Fail'
                    os._exit(1)        

    return 0

def test_sine_gen(testset, extra=[]):
    global test_nb
    
    for ch in testset['channel']:
        for s in testset['sample size']:
            for r in testset['frequency']:
                f = 2 * r
                sf = [random.randint(10, 2 * r / 5) for x in xrange(ch)]
                print '-' * 80
                print 'Test #{}: {} channel(s), {} bytes per sample, sampling rate is {}Hz, sine wave frequency is {}Hz, length is {}s'.format(test_nb, ch, s, r, sf, f / r)
                command = generate_command(ch, s, r, f, sf, None, extra)
                test_nb += 1
                print '  Calling bat with cmd:', " ".join(command)
                launch_bat(command, args.verbose)
                print ' ==> Pass'    
    return 0

def test_arg_n_single_line(testset):
    global test_nb
    
    for ch in testset['channel']:
        for s in testset['sample size']:
            for r in testset['frequency']:
                expected_duration = float(random.randint(5, 40)) / 10
                f = str(expected_duration) + 's';
                sf = [random.randint(10, 2 * r / 5) for x in xrange(ch)]
                print '-' * 80
                print 'Test #{}: {} channel(s), {} bytes per sample, sampling rate is {}Hz, sine wave frequency is {}Hz, playback length is {}s'.format(test_nb, ch, s, r, sf, expected_duration)
                command_p = generate_command(ch, s, r, f, sf, None, extra=[], dev='-P')
                test_nb += 1
                print '  Calling bat with cmd:\n', " ".join(command_p)
                
                real_duration = launch_bat(command_p, args. verbose, check_time=True)
                print '  BAT was expected to run ', expected_duration, 's and run for ', real_duration, 's'
                
                if ((real_duration > expected_duration - 0.1) and (real_duration < expected_duration + 0.1)):
                    print ' ==> Pass'    
                else:
                    print ' ==> Fail'
                    os._exit(1)        
    return 0
    pass

def test_single_line(testset, extra=[]):
    global test_nb
    
    for ch in testset['channel']:
        for s in testset['sample size']:
            for r in testset['frequency']:
                f = r
                sf = [random.randint(10, 2 * r / 5) for x in xrange(ch)]
                print '-' * 80
                print 'Test #{}: {} channel(s), {} bytes per sample, sampling rate is {}Hz, sine wave frequency is {}Hz, playback length is {}s, capture length is {}s'.format(test_nb, ch, s, r, sf, 2 * f / r, f / r)
                command_p = generate_command(ch, s, r, 2 * f, sf, None, extra, dev='-P')
                command_c = generate_command(ch, s, r, f, sf, None, extra, dev='-C')
                test_nb += 1
                print '  Calling twice bat with cmd:\n', " ".join(command_p), "\n", " ".join(command_c)
                single_playback = threading.Thread(None, launch_bat, "single_playback", (command_p, args.verbose))
                single_capture = threading.Thread(None, launch_bat, "single_capture", (command_c, args.verbose))
                single_playback.start()
                time.sleep(0.1)
                single_capture.start()
                single_playback.join()
                single_capture.join()
                print ' ==> Pass'
    return 0

    
def test_input_file(testset, extra=[]):
    global test_nb
    
    for ch in testset['channel']:
        for s in testset['sample size']:
            for r in testset['frequency']:
                f = 2 * r
                sf = [random.randint(10, 2 * r / 5) for x in xrange(ch)]
                print '-' * 80
                print 'Test #{}: {} channel(s), {} bytes per sample, sampling rate is {}Hz, sine wave frequency is {}Hz, analysing length is {}s'.format(test_nb, ch, s, r, sf, f / r)
                print '  Generating wav file: {} channel(s), {} bytes per sample, sampling rate is {}Hz, sine wave frequency is {}Hz, length is {}s'.format(ch, s, r, sf, f * 2 / r)
                generate_wav_file(ch, s, r, f, sf)
                command = generate_command(None, None, None, None, sf, wav_file, extra)
                test_nb += 1
                print '  Calling bat with cmd:', " ".join(command)
                launch_bat(command, args.verbose)
                print ' ==> Pass'
    return 0

def parse_command_line():
    global args
    parser = argparse.ArgumentParser(description='Call BAT with various command line arguments to test BAT.')
    parser.add_argument('-v', '--verbose', help='To get the output of BAT', action='store_true')
    parser.add_argument('-d', '--device', help='Specify the play and capture device', default='plughw:0,0')
    args = parser.parse_args()    
    
# List of tests    
def test_file_analysis(testset, name):
    print '#' * 80
    print '#' * 10, 'TESTING BAT ANALYZIS -- ', name, '#' * 10
    test_input_file(testset_alsa, ['-l'])
        
def test_file_loopback(testset, name, extra=[]):
    print '#' * 80
    print '#' * 10, 'TESTING BAT AUDIO LOOP -- ', name, '#' * 10
    test_input_file(testset, extra)
    
def test_sine_loopback(testset, name, extra=[]):
    print '#' * 80
    print '#' * 10, 'TESTING BAT AUDIO SINE GEN -- ', name, '#' * 10
    test_sine_gen(testset, extra)  

def test_single_line_mode(testset, name, extra=[]):
    print '#' * 80
    print '#' * 10, 'TESTING SINGLE LINE MODE -- ', name, '#' * 10
    test_single_line(testset, extra)
    
def test_argument_n(name):
    print '#' * 80
    print '#' * 10, 'TESTING ARGUMENT -n -- ', name, '#' * 10
    test_arg_n_sine_gen(testset_arg_n)  
    test_arg_n_single_line(testset_arg_n)

# MAIN    
if __name__ == '__main__':
    parse_command_line()
    
    # Testing plughw by default
    test_file_analysis(testset_alsa, "with ALSA testset")
              
    test_file_loopback(testset_alsa, "ALSA")
    test_sine_loopback(testset_alsa, "ALSA");  
           
    test_file_loopback(testset_tinyalsa, "TINYALSA", ['-t'])
    test_sine_loopback(testset_tinyalsa, "TINYALSA", ['-t'])
          
    test_single_line_mode(testset_alsa, "ALSA")
    test_single_line_mode(testset_tinyalsa, "TINYALSA", ['-t'])
         
    # Testing arguments
    test_argument_n("")

    # Testing hw device
    plug_device = args.device
    args.device = args.device.replace("plug", "")
     
    if (plug_device == args.device):
        sys.exit();
         
    test_file_loopback(testset_hw, "ALSA hw")
    

    # Let's test some expected fails        
    expected_fail = True
    test_file_loopback(testset_hw_fail1, "ALSA hw with not suppored config")
    test_sine_loopback(testset_hw_fail2, "ALSA hw with not suppored config")
    test_file_loopback(testset_hw_fail3, "ALSA hw with not suppored config")
        
    print '\nTESTS FINISHED'

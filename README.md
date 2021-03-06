# Semileptonic VV VBS Selection

## How to run (Interactively)
--------

1. **Make sample list**

**Note**: This is optional, run this step only if samples list is old.

`make_sample_list.sh` script takes `eos` path as argument and, looks for directories within and all the root files within each directory will be put into one text file.
For example, (2nd argument is the subdirectory you want to put the `.txt` files in)
```bash
./make_sample_list.sh /eos/uscms/.../2018_parent_sample_dir 2018
```
will produce

```
/eos/uscms/.../2018_parent_sample_dir/sample1` -> `2018/sample1.txt
/eos/uscms/.../2018_parent_sample_dir/sample2` -> `2018/sample2.txt
...
```

where `sample1.txt` will contain all the `.root` files within `/eos/uscms/.../2018_parent_sample_dir/sample1`

2. **Run**

`run.sh` script setups `ROOT` and runs the macro `vbs_flat_ntupler.cc`

Arguments:
```
./run.sh    SAMPLE_LIST_TXT    YEAR    EOS_DIR
SAMPLE_LIST_TXT -> path to txt file,
    .cc macro will extract filename produce root file in current directory
YEAR -> sample year
EOS_DIR (Optional) -> copies .root file to the eos directory, if given.
                      useful when running on condor
```

## How to run on HTCondor at lpc
--------

1. Make the files list as decribed before.

2. Give keyword arguments with `condor_submit`

```bash
condor_submit sample_list_dir=2018 year=2018 eos_output_path=/eos/uscms/store/user/singhr/test/ condor_submit.jdl
```

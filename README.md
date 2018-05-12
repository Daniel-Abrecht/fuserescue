# fuserescue

**WARNING**: This is a new and mostly untested tool. Use at your own risk.
If you already have a mapfile and an image, consider making a backup
ideally of both, or at least of the mapfile before using fuserescue.

## What does it do
A CLI tool to recover only the parts of a disk accessed and not yet recovered.
It uses the same mapfile format as ddrescue to store which parts have already
been recovered and which parts failed. Fuse is used to mount a virtual file.
Any read operation on that file will either take the already recovered data
from the image file, or try to recover the data if data recovery has been enabled.

## When should it be used, and when shouldn't it be used
This Program can be used if you want to recover the partition table or mount
a partition of a drive and recover a few important files, without having to
recover the whole drive or having to lookup and calculate which offsets have
to be recovered. But this will only work if the necessary parts to mount the
partition for example can still be fully recovered.

Be aware that this program is less efficient than ddrescue. It doesn't first
try to read larger areas to recover as much data as possible first, for example.
It also does more seeking than ddrescue. When trying to recover a failing disk,
this additional load can cause the drive to fail sooner. If you can keep ddrescue
running and don't have that one file you want to safe first, you shouldn't
use fuserescue. If you can't use ddrescue, because the drive stops responding
after a short time before any meaningful amount of data can be recovered,
but each sector could still be recovered with a few tries, fuserescue may
be a sensible choice. But a disk in such a condition will probably fail after
trying to rescue a few files or even sooner entirely, so you better know what you
want to recover beforehand and type fast in that case.

fuserescue and ddrescue may also be used together, but not at the same time.
For example, you could use fuserescue with mapfiles from ddrescue, in case
recovering a significant amount of data with ddrescue turns out to be infeasible.
Or you could use fuserescue together with ```fdisk -l mountpoint``` or
```losetup -f -Pr --show mountpoint``` to recover the partition table and in the
lather case maybe some extra Information, whatever the kernel and udev are
accessing. Be aware that this can be a lot more than one may expect. After that,
you could use the mapfile from fuserescue with ddrescue again to recover the rest
of the disk.

Another possible but unintended use case of fuserescue is to use the image and the
mapfile to simulate the failed drive. You could use it like this:
```
fuserescue image image mapfile mountpoint <&- >&-
```
Fuserescue doesn't try to recover any data until "recovery allow" has been typed
at the interactive prompt. If a read of an unrecovered area is attempted and
recovery hasn't been allowed, fuserescue will return an EIO error for the read
attempt, just like a failing disk might have done. By using the program without
stdin and stdout, and using the same image as input and output file, we can
essentially use it to simulate the filed disk using the data recovered according
to the mapfile. But since this is kind of a hack, this method shouldn't be used.
Maybe I'll implement a proper way to do this someday.

## Common pitfalls & important operation details
Per default, fuserescue opens the file to rescue using direct io. This is usually
fine for if that file is a block device, but it may cause problems with regular
files depending on their file size, the file system they reside on, and other
factors. You can disable direct io from the file to recover using the
```--infile-no-direct-io``` option.

Per default, fuse rescue tries to use the blocksize determined from the file to
recover using the BLKSSZGET, and defaults to 512 otherwise. But offsets and size
information are always in bytes. Since the blocksize, and with it the largest chunk
of data it will try to read at a time, is usually only 512, I recommend setting a
larger block size right at the beginning. Something like ```blocksize 0x1000```
should suffice. many programs and the OS may make smaller reads anyway. The
OS may also combine some reads. To disable this interference of the OS, you
can use the ```--fuse-direct-io``` option, but it's usually better not to use it.

If the file to recover is larger than the image file, fuserescue will increase
the size of the image to match the size of the file to recover immediately.
fuserescue needs a file system which supports sparse files in order to do this.
Be aware that sometimes file systems and partition tables may have backups at the
end of a disk or partition. By accidentally increasing the image file to a size
too large, the OS or other tools may no longer find them. For now, either write
down the size somewhere and lower it again using a program like "truncate",
or be especially careful what parameters you specify. Maybe I should change this
at some point.

The offset parameter affects only reading data from the file to recover, it
isn't applied to the image when writing or reading. The offset is subtracted
from the size of the file to recover, except if a size is explicitly specified.

The mapfile is saved after each recovery attempt/fuse read call, if it changed.
It is also saved when closing the program.

Changes to the blocksize for reads and the settings which areas are allowed
to be recovered won't affect recovery attempt/fuse read call that are already
in progress. Only the next recovery attempt will be affected.

If fuserescue fails to recover some data, it marks that area as nonscraped in
it's mapfile, even if some parts of it have been marked as bad sectors before.
While recovering data, it may internally marks some areas as nontried, but that
usually only becomes apparent when manually saving or displaying the mapfile.

If the recovery of some data failed, and you want to try to recover them again,
you need to allow the tool to recover areas marked as nonscraped.

## Usage

Make sure you've read everything before this section carefully before you try to
use it.

### The fuserescue command and arguments

```
fuserescue [--infile-no-direct-io|--fuse-direct-io] infile outfile mapfile mountpoint [offset] [size]
```

| Argument     | Description |
| ------------ | ----------- |
| `infile`     | The file to recover |
| `outfile`    | The image file where newly recovered data are written to and where already recovered data are read from |
| `mapfile`    | A mapfile compatible with ddrescue, containing a list of which areas have already been recovered and which ones failed |
| `mountpoint` | A regular file used as mountpoint for the virtual fuse file representing an image with recover on access functionality. I recommend just creating an empty file (```touch mountpoint```) |
| `offset`     | Optional. Adds an offset to each read from the input file. This may be useful if you started recovering a partition and know it's offset, but later the OS was no longer able to read the partition table |
| `size`       | Optional. Overrides the detected input and image file size. May be useful for the same reason as `offset`. |

| Option | Description |
| ------ | ----------- |
| `--infile-no-direct-io` | Disable the usage of direct io for reading from the file to recover |
| `--fuse-direct-io`      | Enable direct io for the virtual fuse file. This prevents the OS mostly from combining and splitting different reads. |


### CLI commands

| Command                | Description |
| ---------------------- | ----------- |
| help                   | Displays a list of commands |
| save [mapfile]         | Saves the mapfile |
| exit                   | Exits the program after the current recovery attempt has finished |
| recovery allow\|deny   | Allow or Deny reading from the file to recover |
| recovery allow\|deny nontried\|nontrimed\|nonscraped\|badsector | Allow or deny the recovery of areas marked as nontried, nontrimmed, etc. |
| recovery show          | Show the current state of what the program is allowed to try to recover |
| show map               | Display the mapfile |
| show license           | Display the GPL License this program uses |
| show readme            | Display the readme |
| reopen [infile]        | Reopen file to recover. You can optionally specify the file if it changed location |
| blocksize [number]     | Get or set biggest unit of data tried to recover at once. Decimal, hexadecimal and octal notation are possible |
| loglevel default\|info | Get or set loglevel. Default only shows errors. Info also shows read attempts from the image and from the file to recover. |


### Enironment variables

| Environment variable | Description |
| -------------------- | ----------- |
| PAGER                | Set the pager program |
| MDPAGER              | Set the pager for markdown. Unlike $PAGER, this allows for most regular shell command lines, only the main program should be at the beginning. |

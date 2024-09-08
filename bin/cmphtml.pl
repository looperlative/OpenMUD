#!/usr/bin/perl

use Data::Dumper qw(Dumper);
use File::Compare;
use File::Copy;

my $newfile = 'lib/mudwho.html';
my $oldfile = 'lib/mudwho.html.last';

my $exists = -s $oldfile;
my $same = compare($newfile, $oldfile);

if (! -s $oldfile || compare($newfile, $oldfile) != 0) {
    copy($newfile, $oldfile);

    open(IFILE, '<', $oldfile) or die "Couldn't open $oldfile";
    $players = [];
    foreach $l (<IFILE>) {
	if ($l =~ /^\[([0-9]+) ([A-Za-z]+)\] ([A-Za-z]+) (.*)$/) {
	    push @$players, ($1, $2, $3, $4);
	}
    }
    close(IFILE);

    open(OFILE, '>', 'lib/www/mudwho.html') or die "Couldn't open lib/www/mudwho.html";
    print OFILE "<HTML>\n<HEAD>\n<TITLE>Who is on the Mud?</TITLE>\n";
    print OFILE '<link rel="stylesheet" href="mudwho.css">' . "\n</HEAD>\n";
    print OFILE "<BODY>\n<H1>Who is playing right now?</H1>\n<HR>\n";
    print OFILE "<table>\n<tr>\n<td>Lvl</td>\n<td>Class</td>\n<td>Name</td>\n<td>Title</td>\n</tr>\n";
    foreach $p ($players) {
	print OFILE "<tr>\n";
	for my $i (@$p) {
	    print OFILE "<td>$i</td>\n";
	}
	print OFILE "</tr>\n</table>\n</BODY>\n</HTML>\n";
    }
    close(OFILE);
}

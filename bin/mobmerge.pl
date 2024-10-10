#!/usr/bin/perl

use Data::Dumper qw(Dumper);

#
# Get the names of all of the zone files.
#
my $zondirname = 'lib/world/zon';
opendir(ZDIR, $zondirname) or die "Couldn't open zone directory $zondirname\n";

my @zonfiles;
my $f;
while ($f = readdir(ZDIR)) {
    if ($f =~ /^(.*)\.zon$/) {
	push @zonfiles, $1;
    }
}

closedir(ZDIR);

#
# Sort the list of files by zone number.
#
@zonfiles = sort { $a <=> $b } @zonfiles;
for my $i (0 .. $#zonfiles) {
    $zonfiles[$i] = $zondirname . "/" . $zonfiles[$i] . ".zon";
}

#
# Read zone min and max numbers from zone files.
#
my %zmin;
my %zmax;
my %zreverse;
foreach my $zname (@zonfiles) {
    open ($zonefh, $zname) or die "Ack!!  Couldn't open $zname.\n";

    my $line = <$zonefh>;
    if ($line =~ /^#([0-9]+)$/) {
	my $znum = $1;
	my $zname = <$zonefh>;
	my @fields = split / +/, <$zonefh>;
	$zmin{$znum} = $fields[0];
	$zmax{$znum} = $fields[1];

	my $i1 = int($zmin{$znum}/100);
	my $i2 = int($zmax{$znum}/100);
	for my $i ($i1..$i2) {
	    $zreverse{$i} = $znum;
	}
    }

    close ($zonefh);
}

#
# Get the names of all of the mobile files.
#
my $mobdirname = 'lib/world/mob';
opendir(MDIR, $mobdirname) or die "Couldn't open mobile directory $mobdirname\n";

my @mobfiles;
my $f;
while ($f = readdir(MDIR)) {
    if ($f =~ /^(.*)\.mob$/) {
	push @mobfiles, $1;
    }
}

closedir(MDIR);

#
# Sort the list of files by zone number.
#
@mobfiles = sort { $a <=> $b } @mobfiles;
for my $i (0 .. $#mobfiles) {
    $mobfiles[$i] = $mobdirname . "/" . $mobfiles[$i] . ".mob";
}

#
# Read each mobile from each file.
#
my $mnum;
my %mdb;
foreach my $fname (@mobfiles) {
    open($mobfh, $fname) or die "Ack!!  Couldn't open $fname.\n";

    my $m = "";
    my $mnum = 0;
    while (!eof($mobfh)) {
	my $line = <$mobfh>;
	last unless defined $line;

	if ($line =~ /^#([0-9]+)$/) {
	    if ($mnum > 0) {
		$mdb{$mnum} = $m;
	    }
	    $m = $line;
	    $mnum = $1;
	} elsif ($line =~ /^\$$/) {
	    break;
	} else {
	    $m .= $line;
	}
    }
    if ($mnum > 0) {
	$mdb{$mnum} = $m;
    }

    close($mobfh);
}

# foreach my $i (sort { $a <=> $b } keys %odb) {
#     print "==================== $i ====================\n";
#     print $mdb{$i};
# }

#
# Read edits and merge with exist mobects.
#
my %changed_zones;
my $meditfname = $mobdirname . "/medit.mob";
if (open($mobfh, $meditfname)) {
    my $m = "";
    my $mnum = 0;
    while (!eof($mobfh)) {
	my $line = <$mobfh>;
	last unless defined $line;

	$line =~ s/\r//g;
	if ($line =~ /^#([0-9]+)$/) {
	    if ($mnum > 0) {
		my $n = int($mnum/100);
		if (exists $zreverse{$n})
		{
		    $mdb{$mnum} = $m;
		    $changed_zones{$zreverse{$n}} = 1;
		}
	    }
	    $m = $line;
	    $mnum = $1;
	} else {
	    $m .= $line;
	}
    }
    if ($mnum > 0) {
	my $n = int($mnum/100);
	if (exists $zreverse{$n})
	{
	    $mdb{$mnum} = $m;
	    $changed_zones{$zreverse{$n}} = 1;
	}
    }

    close($mobfh);
}

foreach my $zone (keys %changed_zones) {
    $fname = $mobdirname . "/" . $zone . ".mob.new";
    if (open($outfh, ">", $fname)) {
	my $i1 = $zmin{$zone};
	my $i2 = $zmax{$zone};

	for my $i ($i1..$i2) {
	    if (exists $mdb{$i}) {
		print $outfh $mdb{$i};
	    }
	}

	print $outfh "\$\n";
	close($outfh);

	$oldfile = $mobdirname . "/" . $zone . ".mob.old";
	$activefile = $mobdirname . "/" . $zone . ".mob";

	unlink $oldfile;
	rename $activefile, $oldfile;
	rename $fname, $activefile;
    }
}

unlink $meditfname;

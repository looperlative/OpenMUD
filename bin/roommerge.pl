#!/usr/bin/perl

use Data::Dumper qw(Dumper);

#
# Get the names of all of the zone files.
#
my $zondirname = 'lib/world/zon';
opendir(ZDIR, $zondirname) or die "Couldn't open zone directory $zondirname\n";

my @roomfiles;
my $f;
while ($f = readdir(ZDIR)) {
    if ($f =~ /^(.*)\.zon$/) {
	push @roomfiles, $1;
    }
}

closedir(ZDIR);

#
# Sort the list of files by zone number.
#
@roomfiles = sort { $a <=> $b } @roomfiles;
for my $i (0 .. $#roomfiles) {
    $roomfiles[$i] = $zondirname . "/" . $roomfiles[$i] . ".zon";
}

#
# Read zone min and max numbers from zone files.
#
my %zmin;
my %zmax;
my %zreverse;
foreach my $zname (@roomfiles) {
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
# Get the names of all of the room files.
#
my $roomdirname = 'lib/world/wld';
opendir(RDIR, $roomdirname) or die "Couldn't open mobile directory $roomdirname\n";

my @roomfiles;
my $f;
while ($f = readdir(RDIR)) {
    if ($f =~ /^(.*)\.wld$/) {
	push @roomfiles, $1;
    }
}

closedir(RDIR);

#
# Sort the list of files by zone number.
#
@roomfiles = sort { $a <=> $b } @roomfiles;
for my $i (0 .. $#roomfiles) {
    $roomfiles[$i] = $roomdirname . "/" . $roomfiles[$i] . ".wld";
}

#
# Read each mobile from each file.
#
my $rnum;
my %rdb;
foreach my $fname (@roomfiles) {
    open($roomfh, $fname) or die "Ack!!  Couldn't open $fname.\n";

    my $r = "";
    my $rnum = 0;
    while (!eof($roomfh)) {
	my $line = <$roomfh>;
	last unless defined $line;

	if ($line =~ /^#([0-9]+)$/) {
	    if ($rnum > 0) {
		$rdb{$rnum} = $r;
	    }
	    $r = $line;
	    $rnum = $1;
	} elsif ($line =~ /^\$$/) {
	    break;
	} else {
	    $r .= $line;
	}
    }
    if ($rnum > 0) {
	$rdb{$rnum} = $r;
    }

    close($roomfh);
}

# foreach my $i (sort { $a <=> $b } keys %odb) {
#     print "==================== $i ====================\n";
#     print $rdb{$i};
# }

#
# Read edits and merge with exist mobects.
#
my %changed_zones;
my $reditfname = $roomdirname . "/redit.wld";
if (open($roomfh, $reditfname)) {
    my $r = "";
    my $rnum = 0;
    while (!eof($roomfh)) {
	my $line = <$roomfh>;
	last unless defined $line;

	$line =~ s/\r//g;
	if ($line =~ /^#([0-9]+)$/) {
	    if ($rnum > 0) {
		my $n = int($rnum/100);
		if (exists $zreverse{$n})
		{
		    $rdb{$rnum} = $r;
		    $changed_zones{$zreverse{$n}} = 1;
		}
	    }
	    $r = $line;
	    $rnum = $1;
	} else {
	    $r .= $line;
	}
    }
    if ($rnum > 0) {
	my $n = int($rnum/100);
	if (exists $zreverse{$n})
	{
	    $rdb{$rnum} = $r;
	    $changed_zones{$zreverse{$n}} = 1;
	}
    }

    close($roomfh);
}

foreach my $zone (keys %changed_zones) {
    $fname = $roomdirname . "/" . $zone . ".wld.new";
    if (open($outfh, ">", $fname)) {
	my $i1 = $zmin{$zone};
	my $i2 = $zmax{$zone};

	for my $i ($i1..$i2) {
	    if (exists $rdb{$i}) {
		print $outfh $rdb{$i};
	    }
	}

	print $outfh "\$\n";
	close($outfh);

	$oldfile = $roomdirname . "/" . $zone . ".wld.old";
	$activefile = $roomdirname . "/" . $zone . ".wld";

	unlink $oldfile;
	rename $activefile, $oldfile;
	rename $fname, $activefile;
    }
}

unlink $reditfname;

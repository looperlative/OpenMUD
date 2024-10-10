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
# Get the names of all of the object files.
#
my $objdirname = 'lib/world/obj';
opendir(ODIR, $objdirname) or die "Couldn't open object directory $objdirname\n";

my @objfiles;
my $f;
while ($f = readdir(ODIR)) {
    if ($f =~ /^(.*)\.obj$/) {
	push @objfiles, $1;
    }
}

closedir(ODIR);

#
# Sort the list of files by zone number.
#
@objfiles = sort { $a <=> $b } @objfiles;
for my $i (0 .. $#objfiles) {
    $objfiles[$i] = $objdirname . "/" . $objfiles[$i] . ".obj";
}

#
# Read each object from each file.
#
my $onum;
my %odb;
foreach my $fname (@objfiles) {
    open($objfh, $fname) or die "Ack!!  Couldn't open $fname.\n";

    my $o = "";
    my $onum = 0;
    while (!eof($objfh)) {
	my $line = <$objfh>;
	last unless defined $line;

	if ($line =~ /^#([0-9]+)$/) {
	    if ($onum > 0) {
		$odb{$onum} = $o;
	    }
	    $o = $line;
	    $onum = $1;
	} elsif ($line =~ /^\$$/) {
	    break;
	} else {
	    $o .= $line;
	}
    }
    if ($onum > 0) {
	$odb{$onum} = $o;
    }

    close($objfh);
}

# foreach my $i (sort { $a <=> $b } keys %odb) {
#     print "==================== $i ====================\n";
#     print $odb{$i};
# }

#
# Read edits and merge with exist objects.
#
my %changed_zones;
my $oeditfname = $objdirname . "/oedit.obj";
if (open($objfh, $oeditfname)) {
    my $o = "";
    my $onum = 0;
    while (!eof($objfh)) {
	my $line = <$objfh>;
	last unless defined $line;

	if ($line =~ /^#([0-9]+)$/) {
	    if ($onum > 0) {
		my $n = int($onum/100);
		if (exists $zreverse{$n})
		{
		    $odb{$onum} = $o;
		    $changed_zones{$zreverse{$n}} = 1;
		}
	    }
	    $o = $line;
	    $onum = $1;
	} else {
	    $o .= $line;
	}
    }
    if ($onum > 0) {
	my $n = int($onum/100);
	if (exists $zreverse{$n})
	{
	    $odb{$onum} = $o;
	    $changed_zones{$zreverse{$n}} = 1;
	}
    }

    close($objfh);
}

foreach my $zone (keys %changed_zones) {
    $fname = $objdirname . "/" . $zone . ".obj.new";
    if (open($outfh, ">", $fname)) {
	my $i1 = $zmin{$zone};
	my $i2 = $zmax{$zone};

	for my $i ($i1..$i2) {
	    if (exists $odb{$i}) {
		print $outfh $odb{$i};
	    }
	}

	print $outfh "\$\n";
	close($outfh);

	$oldfile = $objdirname . "/" . $zone . ".obj.old";
	$activefile = $objdirname . "/" . $zone . ".obj";

	unlink $oldfile;
	rename $activefile, $oldfile;
	rename $fname, $activefile;
    }
}

unlink $oeditfname;

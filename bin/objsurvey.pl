#!/usr/bin/perl

use Data::Dumper qw(Dumper);

#
# Command line arguments
#
my $argc = @ARGV;
my $command = 'all';
if ($argc >= 1) {
    $command = $ARGV[0];
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
foreach my $fname (@objfiles) {
    open (OFILE, $fname) or die "Ack!!  Couldn't open $fname.\n";

    my %odb;
    my $o = {};
    my $edescs = [];
    my $affects = [];
    while (!eof(OFILE)) {
	my $line = <OFILE>;
	last unless defined $line;

	if ($line =~ /^#([0-9]+)$/) {
	    if (keys %{$o}) {
		$odb{$o->{'number'}} = $o;
	    }
	    $o = {};
	    $edescs = [];
	    $affects = [];
	    $o->{'number'} = $1;
	    $o->{'alias list'} = <OFILE>;
	    $o->{'short description'} = <OFILE>;
	    $o->{'long description'} = <OFILE>;
	    $o->{'action description'} = <OFILE>;

	    my @fields = split / +/, <OFILE>;
	    $o->{'type'} = $fields[0];
	    $o->{'effectsbits'} = $fields[1];
	    $o->{'wearbits'} = $fields[2];

	    @fields = split / +/, <OFILE>;
	    $o->{'value0'} = $fields[0];
	    $o->{'value1'} = $fields[1];
	    $o->{'value2'} = $fields[2];
	    $o->{'value3'} = $fields[3];

	    @fields = split / +/, <OFILE>;
	    $o->{'weight'} = $fields[0];
	    $o->{'cost'} = $fields[1];
	    $o->{'rent'} = $fields[2];
	}
	elsif ($line =~ /^E/) {
	    my $keywords = <OFILE>;
	    my $description = "";
	    while (!eof(OFILE)) {
		$line = <OFILE>;
		last unless ($line !~ /^~/);
		$description = $description . $line;
	    }

	    push @$edescs, ($keywords, $description);
	    $o->{'edescs'} = $edescs;
	}
	elsif ($line =~ /^A/) {
	    @fields = split / +/, <OFILE>;
	    my $location = $fields[0];
	    my $value = $fields[1];

	    push @$affects, ($location, $value);
	    $o->{'affects'} = $affects;
	}
    }
    if (keys %{$o}) {
	$odb{$o->{'number'}} = $o;
    }

    close (OFILE);

    if ($command eq 'all') {
	foreach my $k (sort keys %odb) {
	    print "$k\n";
	    print Dumper($odb{$k});
	}
    }
    elsif ($command eq 'weapons') {
	foreach my $k (sort keys %odb) {
	    if ($odb{$k}->{'type'} == 5) {
		$odb{$k}->{'short description'} =~ /^(.*)~$/;
		my $short = $1;
		my $ndice = $odb{$k}->{'value1'};
		my $diesize = $odb{$k}->{'value2'};
		my $avgdam = ($diesize + 1.0) * 0.5 * $ndice;
		print "$k\t$avgdam\t$ndice\t$diesize\t$short\n";
	    }
	}
    }
    else {
	print "Command needs to be 'all' or 'weapons'\n";
    }
}

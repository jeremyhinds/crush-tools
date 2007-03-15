#!/usr/bin/perl -w

use strict;
use Getopt::Std;
use vars qw($opt_h $opt_v $opt_d $opt_e $opt_i $opt_p $opt_b $opt_c $opt_r);
use Date::Calc qw(Delta_DHMS);
use DART::DateFormat;
use Text::Trim;

# Parse command line options
getopts('hvprd:e:i:b:c:');

# Define default delimiter
my $delim = `tochar 0xfe`;

# Print help if requested
if ($opt_h) {
    print << "HELP";

usage: $0 [-h] [-v] [-p] [-d <delimiter>] [-i <index>] -e <expression> -b <fallback_result> [-c <column_name>]

 -h                       .. help
 -v                       .. verbose (for debugging purposes)
 -p			  .. preserve the header line (just passes the first line through to the output file)
 -r 			  .. replace the value at the given index (instead of adding a new column)
 -d <delimiter>           .. the delimiter for the input file which is read from the standard input; defaults to $delim
 -i <index>               .. index for the new fields (1-based); if not given the new field will be appended
 -e <expression>          .. the expression to calculate; the expression may contain references to fields, e.g. [1] + [2]
 -b <fallback_result>     .. if the formula is not properly evaluated use this result as the fallback
 -c <column_name>         .. the name of the column for the calculated field; only used with option -p

HELP

   exit(0);
}

# Evaluate event ids
my $line = "";
my @parts = ();
my $i = 0;
my $result = 0;

# Check delimiter option. Set default if not provided.
if(!defined($opt_d) || $opt_d eq "") {
        $opt_d = $delim;
        print "No delimiter given, assuming $opt_d\n" if(defined($opt_v));
}

# Check index option. Set default if not provided.
if(!defined($opt_i) || !$opt_i =~ m/[1-9][0-9]*/) {
	$opt_i = -1;
        print "No index for the calculated field given, appending the field at the end of the row.\n" if(defined($opt_v));
}

# Check formula. Bail out if it is not given. If given, check the formulas validity.
if(!defined($opt_e)) {
        die "No formula given. Please use option -f to provide one.\n"; exit 1;
} else {
	print "Using formula \"" . $opt_e . "\".\n" if(defined($opt_v));
}

# Check the fallback result. Bail out if it's not given.
if(!defined($opt_b)) {

	# No => But we need one. Bail out.
	die "No fallback result given. Please use option -b to provide one.\n"; exit 1;
}

# Check the column description
if(!defined($opt_c)) {
	$opt_c = "Calculated-Field";
	print "No column name given, using \"$opt_c\" as the column name.\n" if(defined($opt_v));
}

# Should we preserve the header line?
if(defined($opt_p)) {
	
	# Yes => Read and extend the header
	if(defined($line = <STDIN>)) {

		# Remove blanks/new lines at the end of the row
		$line = trim $line;	

		# Split into parts
		@parts = split(/$opt_d/, $line);

		# Build the new header from the original header and the column name for the calculated field.
        	print put_field($line, \@parts, $opt_d, $opt_c, $opt_i, $opt_r) . "\n";
	}

}

# Read the lines from the standard input
while(defined($line = <STDIN>)) {

	# Remove blanks/new lines at the end of the row
	$line = trim $line;

        # Split the line
        @parts = split(/$opt_d/, $line, -1);

	# Calculate the result
	$result = extended_eval($opt_e, \@parts, $opt_b);

	# Build new line with the calculated result
	print put_field($line, \@parts, $opt_d, $result, $opt_i, $opt_r) . "\n";
}

sub put_field {
	my ($line, $parts, $delim, $value, $pos, $repl) = @_;

        # Put the result into the right place. Should we put this at the beginning or end?
        if($pos == -1 || $pos > scalar @{$parts} || (defined($opt_r) && $pos == scalar @{$parts})) {

                # Yes => Should we replace the value?
		if(defined($repl)) {

			# Yes => Remove the last value first
			pop(@{$parts});
			$line = join($delim, @{$parts}) . $delim . $value;
		} else {

			# No => Print the line and the given result at the end.
                	$line = $line . $delim . $value;
		}
        } elsif($pos == 1) {

		# Yes => Should we replace the value?
		if(defined($repl)) {

			# Yes => Remove the first and put the value instead
			shift(@{$parts});
			$line = $value . $delim . join($delim, @{$parts});
		} else {

	                # No => Print the result and the line
        	        $line = $value . $delim . $line;
		}
        } else {

                # Yes => Should we replace the value?
                if(defined($repl)) {

                        # Yes => Cut out the current value
			my @result = split(/$delim/, $line, $pos + 1);
			my $suffix = pop(@result);
			my $old    = pop(@result);

			$line = join($delim, @result) . $delim . $value . $delim . $suffix;
		} else {
	                # No => Put the result in the right place
        	        my @result = split(/$delim/, $line, $pos);
                	my $suffix = pop(@result);

                	$line = join($delim, @result) . $delim . $value . $delim . $suffix;
		}
        }

	return $line;
}

sub extended_eval {
	my($formula, $parts, $fallback) = @_;

	my $result = 0, $i = 0;

	$formula = "\$result = " . $formula;
	for($i = 1; $i <= scalar @{$parts}; $i++) {
		$formula =~ s/\[$i\]/$parts->[$i - 1]/g;
	}

	eval($formula);

	if($@) {
		$result = $fallback;
	}
	
	return $result;
}


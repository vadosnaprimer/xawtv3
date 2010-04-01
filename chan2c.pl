#!/usr/bin/perl

print <<__EOF__;

struct CHAN_FREQ {
    float freq_low;
    float freq_high;
    int   chan_low;
    int   chan_high;
    float factor;
};

__EOF__

foreach $file (@ARGV) {
    $tag = $file;
    $tag =~ s/\.chan//;
    $tag =~ s/.*\///;
    print "struct CHAN_FREQ chan_$tag\[\] = {\n";

    open(FILE,"<$file") || die;
    while(<FILE>) {
	@cols = split;
	next if $#cols != 4;
	print "    { $cols[0], $cols[1], $cols[2], $cols[3], $cols[4] },\n";
    }
    print "    { 0, 0, 0, 0, 0 }\n";
    print "};\n\n";
}

print "struct STRTAB chan_names[] = {\n";
$i = 0;
foreach $file (@ARGV) {
    $tag = $file;
    $tag =~ s/\.chan//;
    $tag =~ s/.*\///;
    print "  { $i, \"$tag\" },\n";
    $i++;
}
print "  { -1, NULL }\n};\n\n";

print "struct CHAN_FREQ *chan_tabs[] = {\n";
foreach $file (@ARGV) {
    $tag = $file;
    $tag =~ s/\.chan//;
    $tag =~ s/.*\///;
    print "  chan_$tag,\n";
}
print "  NULL\n};\n";
    

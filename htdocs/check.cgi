#!/usr/local/bin/perl -Tw

use strict;
use CGI;

my($cgi) = new CGI;

print $cgi->header('text/html');
print $cgi->start_html(-title => "Example CGI script / CGI 示例脚本",
                       -BGCOLOR => 'red');
print $cgi->h1("CGI Example / CGI 示例");
print $cgi->p, "This is an example of CGI\n";
print $cgi->p, "这是一个 CGI 示例\n";
print $cgi->p, "Parameters given to this script:\n";
print $cgi->p, "传递给本脚本的参数：\n";
print "<UL>\n";
foreach my $param ($cgi->param)
{
 print "<LI>", "$param ", $cgi->param($param), "\n";
}
print "</UL>";
print $cgi->end_html, "\n";

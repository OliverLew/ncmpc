#!/usr/bin/env ruby
#
#  (c) 2004-2008 The Music Player Daemon Project
#  http://www.musicpd.org/
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#

#
# Load lyrics from lyrics.wikia.com, formerly lyricwiki.org
#

require 'uri'
require 'net/http'

url = "http://lyrics.wikia.com/api.php?action=lyrics&fmt=xml&func=getSong" + \
    "&artist=#{URI.escape(ARGV[0])}&song=#{URI.escape(ARGV[1])}"
response = Net::HTTP.get(URI.parse(url))

exit(2) unless response =~ /<url>\s*(.*?)\s*<\/url>/im
url = $1.gsub(/wikia.com/, "wikia.com/lyrics");
exit(2) if $1 =~ /action=edit$/

response = Net::HTTP.get(URI.parse(url))
exit(2) unless response =~ /<div class='lyricbox' >\s*(.*?)\s*<!--/im

puts $1.gsub(/<br \/>/, "\n")

/testing/guestbin/swan-prep --46
# confirm that the network is alive
ping6 -n -c 4 2001:db8:1:2::23
# make sure that clear text does not get through
ip6tables -A INPUT -i eth1 -s 2001:db8:1:2::23 -p ipv6-icmp --icmpv6-type echo-reply  -j DROP
# confirm with a ping
ping6 -n -c 4 2001:db8:1:2::23
ipsec setup start
/testing/pluto/bin/wait-until-pluto-started
ipsec auto --add v6-tunnel-east-road
ipsec auto --status
echo "initdone"

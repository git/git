echo "Content-Type: application/x-git-receive-pack-result"
echo
printf '0013\001000eunpack ok\n'
printf '0015\002skipping report\n'
printf '0009\0010000'
printf '0000'

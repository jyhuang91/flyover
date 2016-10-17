./booksim meshconfig > a.txt
grep "READ_REQUEST" ./a.txt > read_request.txt
grep "WRITE_REQUEST" ./a.txt > write_request.txt
grep "READ_REPLY" ./a.txt > read_reply.txt
grep "WRITE_REPLY" ./a.txt > write_reply.txt
wc -l read*
wc -l write*

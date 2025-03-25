Operations Pool:
- given an id, return the op_data
- mark the op_data as free

accept:
- add accept
- add read
- submit

read:
- client disconnects: mark free and cqe seen
- add write
- submit

write:
- handle short writes: add_short_write
- else add_read
- submit

Event Loop:
- Peek 32 CQE
- Handle all
- Single submit
-
- Read Timeout
- Multishot accept
- Multishot recv

TODOs:
- io_uring pdf
- video on io_uring features
- internalize tokio io_uring tcp implementation
- why epoll is fundamentally broken

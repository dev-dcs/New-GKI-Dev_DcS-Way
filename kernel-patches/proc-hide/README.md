# proc-hide — /proc/net/unix manager-socket concealment

`@Dev_DcS`

SUSFS v2.3.0 hides mounts, paths, kstat, maps and the KSU/SUSFS kallsyms
symbols, but it leaves `/proc/net/unix` alone. A root manager keeps an abstract
`AF_UNIX` socket whose name shows up there as an `@name` entry, and a detector
that can no longer see the root files still enumerates that list.

`0001-proc-net-unix-hide-manager-socket.patch` adds two `net.*` sysctls:

| sysctl | default | meaning |
| --- | --- | --- |
| `net.hide_manager_socket` | `0` | master switch |
| `net.hidden_socket_name` | *(empty)* | comma-separated abstract names to drop, no leading `@` |

Both off/empty by default, so stock behaviour is unchanged until you name a
socket. Writing either needs `CAP_SYS_ADMIN`.

Find the name on-device, then feed it back:

```
cat /proc/net/unix | grep '@'
echo '<name>' > /proc/sys/net/hidden_socket_name
echo 1        > /proc/sys/net/hide_manager_socket
```

The hook is one early `return 0` in `unix_seq_show()` plus a file-scope
`late_initcall`; no exported symbol or module-visible struct changes, so the GKI
KMI is untouched. Applies on the fetched tree in the same block as the other
`kernel-patches/*` features — validate the hunk context in CI, `af_unix.c` is
not touched by SUSFS so it should apply clean.

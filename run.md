# run.md: Build, Run and Test Every Feature

This file walks through every implemented feature, one command at a time, with the
output you should see. Run it top to bottom: each step builds on the previous one.

You need **5 terminals**: T1, T2 (trackers) and C1, C2, C3 (clients: alice, bob, carol).

> **Important:** inside the client (`client>` prompt) always use **full paths**
> like `/home/nilkanta/p2ptest/downloads`. The client does **not** expand `~`.
> In a normal shell, `~` works as usual.
> If your username is not `nilkanta`, replace `/home/nilkanta` with your home folder (`echo $HOME`).

---

## 0. Prerequisites

```bash
sudo apt-get install build-essential libssl-dev curl
```

## 1. Build

```bash
cd ~/Desktop/P2P/Peer-To-Peer_File_Sharing
cd tracker && make clean && make && cd ..
cd client  && make clean && make && cd ..
```

Result: `tracker/tracker` and `client/client` exist. Warnings about deprecated
`SHA1_*` functions on OpenSSL 3 are expected and harmless.

## 2. Test files and folders

```bash
mkdir -p ~/p2ptest/alice ~/p2ptest/bob ~/p2ptest/carol ~/p2ptest/downloads
cd ~/p2ptest/alice

# media files (skip any that already exist)
[ -f bunny_5mb.mp4 ]      || curl -fL -o bunny_5mb.mp4      "https://test-videos.co.uk/vids/bigbuckbunny/mp4/h264/720/Big_Buck_Bunny_720_10s_5MB.mp4"
[ -f jellyfish_10mb.mp4 ] || curl -fL -o jellyfish_10mb.mp4 "https://test-videos.co.uk/vids/jellyfish/mp4/h264/1080/Jellyfish_1080_10s_10MB.mp4"
[ -f document_10mb.pdf ]  || curl -fL -A "Mozilla/5.0" -o document_10mb.pdf "https://freetestdata.com/wp-content/uploads/2022/11/Free_Test_Data_10.5MB_PDF.pdf"
[ -f song.mp3 ]           || curl -fL -o song.mp3           "https://download.samplelib.com/mp3/sample-15s.mp3"

# small helper files for error tests
echo "only for group g2" > g2file.txt
touch empty.txt

ls -la ~/p2ptest/alice
sha1sum ~/p2ptest/alice/*.mp4 ~/p2ptest/alice/*.pdf ~/p2ptest/alice/*.mp3

# clean start: remove downloads, logs and saved share lists from earlier runs
rm -f ~/p2ptest/bob/* ~/p2ptest/carol/* ~/p2ptest/downloads/* ~/p2ptest/alice/*.log
rm -f ~/.p2p_seeds_*.txt
```

| File (in `/home/nilkanta/p2ptest/alice`) | Type | Size (bytes) | Pieces (512 KB) |
|---|---|---|---|
| `bunny_5mb.mp4` | video 720p | 4 999 379 | 10 |
| `jellyfish_10mb.mp4` | video 1080p | 10 484 793 | 20 |
| `document_10mb.pdf` | PDF, 57 pages | 11 081 517 | 22 |
| `song.mp3` | audio | 307 453 | 1 |

`tracker/tracker_info.txt` already contains:
```
127.0.0.1 5000
127.0.0.1 5001
```
(`127.0.0.1:5000` style also works.) Trackers also use port +100 (5100/5101)
for syncing with each other, so keep 5000, 5001, 5100, 5101 free.

---

## 3. Start the trackers (primary + backup)

**T1**
```bash
cd ~/Desktop/P2P/Peer-To-Peer_File_Sharing/tracker
./tracker tracker_info.txt 1
```
Expect:
```
[TRACKER] Role set to PRIMARY
[SYNC] Acting as PRIMARY
[SYNC] Primary listening on port 5100
[TRACKER] Listening for clients on port 5000
```

**T2**
```bash
cd ~/Desktop/P2P/Peer-To-Peer_File_Sharing/tracker
./tracker tracker_info.txt 2
```
Expect:
```
[TRACKER] Role set to BACKUP
[SYNC] Acting as BACKUP
[SYNC] Sent SYNC_REQUEST to Primary
[SYNC] Loaded state from primary
```
T1 prints `[SYNC] Backup connected from 127.0.0.1:...`.

✅ **Feature: two trackers, automatic primary/backup roles, initial state sync.**

---

## 4. Start the clients

Each client runs from its own folder (its `downloads.log` / `uploads.log` go there).

**C1 (alice, who has the files)**
```bash
cd ~/p2ptest/alice
~/Desktop/P2P/Peer-To-Peer_File_Sharing/client/client 127.0.0.1:7001 ~/Desktop/P2P/Peer-To-Peer_File_Sharing/tracker/tracker_info.txt
```
**C2 (bob)**
```bash
cd ~/p2ptest/bob
~/Desktop/P2P/Peer-To-Peer_File_Sharing/client/client 127.0.0.1:7002 ~/Desktop/P2P/Peer-To-Peer_File_Sharing/tracker/tracker_info.txt
```
**C3 (carol)**
```bash
cd ~/p2ptest/carol
~/Desktop/P2P/Peer-To-Peer_File_Sharing/client/client 127.0.0.1:7003 ~/Desktop/P2P/Peer-To-Peer_File_Sharing/tracker/tracker_info.txt
```
Each prints `[CLIENT] Connected to PRIMARY tracker at 127.0.0.1:5000`.

✅ **Feature: client finds the primary tracker automatically (`WHOAREYOU`).**

Type `help` in any client to list all commands.

---

## 5. User management

| Client | Command | Expected |
|---|---|---|
| C1 | `create_user alice pw1` | `OK User created: alice` |
| C1 | `create_user alice pw1` | `ERR 500 User already exists` |
| C1 | `create_group media` | `ERR Must be logged in` |
| C1 | `login alice wrong` | `ERR Incorrect password` |
| C1 | `login ghost pw` | `ERR User does not exist` |
| C1 | `login alice pw1` | `[CLIENT] Logged in as alice` |
| C1 | `login alice pw1` | `Already logged in as alice. Run logout before ...` |
| C2 | `create_user bob pw2` | `OK User created: bob` |
| C2 | `login bob pw2` | `Logged in as bob` |
| C3 | `create_user carol pw3` | `OK User created: carol` |
| C3 | `login carol pw3` | `Logged in as carol` |

✅ **Features: registration, duplicate check, password check, login required, one user per client.**

---

## 6. Group management

| Client | Command | Expected |
|---|---|---|
| C1 | `create_group media` | `OK Group created: media` |
| C1 | `create_group media` | `ERR Group already exists` |
| C2 | `list_groups` | `OK Groups:` / ` - media` |
| C2 | `list_files media` | `ERR You are not a member of group media` |
| C2 | `join_group media` | `OK Join request sent for group media` |
| C2 | `join_group media` | `ERR Join request already sent` |
| C3 | `join_group media` | `OK Join request sent for group media` |
| C2 | `list_requests media` | `ERR Only the group owner can view requests` |
| C1 | `list_requests media` | `OK Pending requests for group media:` / ` - bob` / ` - carol` |
| C2 | `accept_request media bob` | `ERR Only the group owner can accept requests` |
| C1 | `accept_request media bob` | `OK User bob added to group media` |
| C1 | `accept_request media carol` | `OK User carol added to group media` |
| C1 | `accept_request media carol` | `ERR No such pending request from carol` |
| C2 | `join_group media` | `ERR Already a member of this group` |
| C1 | `join_group nogroup` | `ERR Group does not exist` |

✅ **Features: create/list/join groups, pending requests, owner-only accept, membership enforcement.**

---

## 7. Upload / share files (alice)

alice's client runs in `/home/nilkanta/p2ptest/alice`, so plain file names work.

| Client | Command | Expected |
|---|---|---|
| C1 | `upload_file media bunny_5mb.mp4` | `Metadata sent to tracker: bunny_5mb.mp4 (4999379 bytes, 10 pieces)` → `OK File bunny_5mb.mp4 uploaded in group media by alice` → `[SEEDER] Now sharing bunny_5mb.mp4 ...` |
| C1 | `upload_file media jellyfish_10mb.mp4` | `(10484793 bytes, 20 pieces)` → `OK ...` |
| C1 | `upload_file media document_10mb.pdf` | `(11081517 bytes, 22 pieces)` → `OK ...` |
| C1 | `upload_file media song.mp3` | `(307453 bytes, 1 pieces)` → `OK ...` |
| C2 | `list_files media` | `OK Files in group media:` / ` - bunny_5mb.mp4` / ` - document_10mb.pdf` / ` - jellyfish_10mb.mp4` / ` - song.mp3` |

Error cases:

| Client | Command | Expected |
|---|---|---|
| C1 | `upload_file media empty.txt` | `[UPLOAD] Empty files cannot be shared` |
| C1 | `upload_file media missing.mp4` | `[UPLOAD] Could not stat file: missing.mp4 (No such file or directory)` |
| C1 | `create_group g2` | `OK Group created: g2` |
| C1 | `upload_file g2 g2file.txt` | `OK File g2file.txt uploaded in group g2 by alice` |
| C2 | `upload_file g2 /etc/hostname` | `ERR You are not a member of group g2` |

Same name, different content, is rejected. First, in a **shell**:
```bash
echo "not really a song" > ~/p2ptest/bob/song.mp3
```
| Client | Command | Expected |
|---|---|---|
| C2 | `upload_file media /home/nilkanta/p2ptest/bob/song.mp3` | `ERR A different file named song.mp3 already exists in group media` |

```bash
rm ~/p2ptest/bob/song.mp3          # remove the fake file again
cat ~/.p2p_seeds_alice.txt         # alice's share list: name <TAB> group <TAB> absolute path
```

✅ **Features: 512 KB pieces, SHA1 per piece and whole file, shared file list saved to disk, upload only into your own groups, name clashes rejected.**

---

## 8. Download (bob): multi-threaded, piece by piece, SHA1-verified

The 3rd argument is the destination: a **folder** (keeps the original name) or a **full file path** (new name).

| Client | Command | Saved as |
|---|---|---|
| C2 | `download_file media bunny_5mb.mp4 /home/nilkanta/p2ptest/downloads` | `/home/nilkanta/p2ptest/downloads/bunny_5mb.mp4` |
| C2 | `download_file media document_10mb.pdf /home/nilkanta/p2ptest/bob/my_document.pdf` | `/home/nilkanta/p2ptest/bob/my_document.pdf` (renamed) |
| C2 | `download_file media song.mp3 /home/nilkanta/p2ptest/downloads` | `/home/nilkanta/p2ptest/downloads/song.mp3` |
| C2 | `show_downloads` | `[C] [media] bunny_5mb.mp4 -> ...`, `[C] [media] document_10mb.pdf -> ...`, `[C] [media] song.mp3 -> ...` |

Each download prints:
```
[CLIENT] Downloading bunny_5mb.mp4 (4999379 bytes, 10 pieces) from 1 peer(s) -> /home/nilkanta/p2ptest/downloads/bunny_5mb.mp4
[CLIENT] Download complete: /home/nilkanta/p2ptest/downloads/bunny_5mb.mp4 (SHA1 verified)
[SEEDER] Now sharing bunny_5mb.mp4 (group media, path /home/nilkanta/p2ptest/downloads/bunny_5mb.mp4)
```

Error cases:

| Client | Command | Expected |
|---|---|---|
| C2 | `download_file media nofile.mp4 /home/nilkanta/p2ptest/downloads` | `ERR File not found in group media` |
| C2 | `download_file media bunny_5mb.mp4 /no/such/dir/x.mp4` | `Cannot create /no/such/dir/x.mp4: No such file or directory` |
| C2 | `download_file media bunny_5mb.mp4 /home/nilkanta/p2ptest/downloads` | `... is a file you are sharing; choose another destination.` (protects the seeded copy) |

Verify in a **shell**:
```bash
cd ~/p2ptest
cmp alice/bunny_5mb.mp4     downloads/bunny_5mb.mp4 && echo "bunny OK"
cmp alice/document_10mb.pdf bob/my_document.pdf     && echo "pdf OK"
cmp alice/song.mp3          downloads/song.mp3      && echo "mp3 OK"
sha1sum alice/bunny_5mb.mp4 downloads/bunny_5mb.mp4

# piece-by-piece log
grep "bunny_5mb.mp4 piece" bob/downloads.log
grep "Sent piece" alice/uploads.log | tail

# open them
xdg-open downloads/bunny_5mb.mp4
xdg-open bob/my_document.pdf
```

✅ **Features: tracker-provided peer list, up to 8 parallel worker threads, per-piece SHA1 check, whole-file SHA1 check, download to a folder or a new file name.**

---

## 9. Downloader becomes a seeder + download from 2 peers (carol)

After step 8, bob also shares `bunny_5mb.mp4`, so carol gets it from **alice and bob**:

| Client | Command | Expected |
|---|---|---|
| C3 | `download_file media bunny_5mb.mp4 /home/nilkanta/p2ptest/carol` | `... from 2 peer(s) -> /home/nilkanta/p2ptest/carol/bunny_5mb.mp4` → `Download complete` |

```bash
cmp ~/p2ptest/alice/bunny_5mb.mp4 ~/p2ptest/carol/bunny_5mb.mp4 && echo "bunny carol OK"
grep "bunny_5mb.mp4 piece" ~/p2ptest/carol/downloads.log | grep -o "from 127.0.0.1:700[0-9]" | sort | uniq -c
```
Pieces come from both `7001` (alice) and `7002` (bob), e.g. `6 from ...7001`, `4 from ...7002`.

✅ **Features: automatic seeding after download, random peer choice, load spread across peers.**

---

## 10. Concurrent downloads + progress

In **C3**, type these three lines quickly, one after another:
```
download_file media jellyfish_10mb.mp4 /home/nilkanta/p2ptest/carol
download_file media song.mp3 /home/nilkanta/p2ptest/carol
show_downloads
```
While a download is still running, `show_downloads` shows a progress line:
```
[D] [media] jellyfish_10mb.mp4 : 12/20 pieces (60%) -> /home/nilkanta/p2ptest/carol/jellyfish_10mb.mp4
```
Run `show_downloads` again: everything is `[C]` (finished). On localhost 10 MB finishes
in well under a second, so you may only see `[C]`.

```bash
cmp ~/p2ptest/alice/jellyfish_10mb.mp4 ~/p2ptest/carol/jellyfish_10mb.mp4 && echo "jellyfish carol OK"
cmp ~/p2ptest/alice/song.mp3           ~/p2ptest/carol/song.mp3           && echo "song carol OK"
```

✅ **Features: several downloads at once, live progress (`[D]` running / `[C]` complete).**

---

## 11. Bad peer during download (retry on other peers)

carol now shares `jellyfish_10mb.mp4`. Make her copy unreadable **while she stays
logged in**, so the tracker still offers her as a peer. In a **shell**:
```bash
mv ~/p2ptest/carol/jellyfish_10mb.mp4 ~/p2ptest/carol/jellyfish.bak
```
| Client | Command | Expected |
|---|---|---|
| C2 | `download_file media jellyfish_10mb.mp4 /home/nilkanta/p2ptest/bob` | `... from 2 peer(s)` → still `Download complete ... (SHA1 verified)` |

```bash
grep refused ~/p2ptest/bob/downloads.log | head -3
#   Peer 127.0.0.1:7003 refused piece 7: ERR Could not stat file
grep "jellyfish_10mb.mp4 piece" ~/p2ptest/bob/downloads.log | grep -o "from 127.0.0.1:700[0-9]" | sort | uniq -c
#   all 20 pieces from 7001 (alice)
cmp ~/p2ptest/alice/jellyfish_10mb.mp4 ~/p2ptest/bob/jellyfish_10mb.mp4 && echo "jellyfish bob OK"

mv ~/p2ptest/carol/jellyfish.bak ~/p2ptest/carol/jellyfish_10mb.mp4     # restore
```

✅ **Feature: a piece that fails from one peer is fetched from another peer.**

---

## 12. Stop sharing

`song.mp3` is now shared by alice (uploader), bob (step 8) and carol (step 10).

| Client | Command | Expected |
|---|---|---|
| C1 | `stop_share media song.mp3` | `OK alice stopped sharing song.mp3 in group media` |
| C1 | `list_files media` | `song.mp3` **still listed** (bob and carol share it) |
| C2 | `stop_share media song.mp3` | `OK bob stopped sharing song.mp3 in group media` |
| C3 | `stop_share media song.mp3` | `OK carol stopped sharing song.mp3 in group media` |
| C1 | `list_files media` | `song.mp3` is **gone** (nobody shares it) |
| C3 | `stop_share media song.mp3` | `ERR File song.mp3 not found in group media` |

✅ **Feature: stop_share removes you as a seeder; the file disappears when nobody shares it.**

---

## 13. Logout, login again, shared files restored

| Client | Command | Expected |
|---|---|---|
| C2 | `logout` | `OK Logged out bob` / `Local uploader server stopped.` |
| C2 | `show_downloads` | `Not logged in.` |
| C3 | `download_file media document_10mb.pdf /home/nilkanta/p2ptest/carol/doc_copy.pdf` | works; bob (port 7002) is **not** a peer while logged out |
| C2 | `login bob pw2` | `Logged in as bob` / `[SEEDER] Restored 3 shared file(s) for bob` + the list (bunny, document, jellyfish) |

Restarting also keeps them: `exit` in C2, start the bob client again (step 4), `login bob pw2` restores the same list.

```bash
grep "document_10mb.pdf piece" ~/p2ptest/carol/downloads.log | grep -c "7002"   # prints 0: no pieces from bob
cmp ~/p2ptest/alice/document_10mb.pdf ~/p2ptest/carol/doc_copy.pdf && echo "pdf carol OK"
```

✅ **Features: logout, offline users are not offered as peers, shared files survive logout and restart.**

---

## 14. Tracker failover (primary dies)

1. C2: `list_files media` → note the list.
2. In **T1** type `quit` (or press Ctrl+C).
3. T2 prints:
   ```
   [SYNC] Peer disconnected, promoting to PRIMARY
   [TRACKER] Role set to PRIMARY
   [TRACKER] Listening for clients on port 5001
   ```
4. C2: `list_files media` →
   ```
   [CLIENT] Lost connection to tracker, looking for the primary...
   [CLIENT] Connected to PRIMARY tracker at 127.0.0.1:5001
   [CLIENT] Session restored for bob
   [CLIENT] Reconnected. Please re-run your last command.
   ```
5. C2: `list_files media` → the same list as before.
6. Download through the new primary:

   | Client | Command |
   |---|---|
   | C2 | `download_file media bunny_5mb.mp4 /home/nilkanta/p2ptest/bob/bunny_after_failover.mp4` |

   ```bash
   cmp ~/p2ptest/alice/bunny_5mb.mp4 ~/p2ptest/bob/bunny_after_failover.mp4 && echo "failover download OK"
   ```
C1 and C3 reconnect the same way on their next command.

✅ **Features: users, groups, files and logins are mirrored to the backup; automatic promotion; clients reconnect and log in again by themselves.**

---

## 15. Old primary rejoins as backup

1. In **T1** start it again:
   ```bash
   ./tracker tracker_info.txt 1
   ```
   It prints `Acting as BACKUP` and `Loaded state from primary`
   (the `SYNC_RESPONSE USER alice ... GROUP media ... FILE media bunny_5mb.mp4 ...` line shows the copied state).
2. C2: `create_group g3` → T1 prints `[SYNC] Applied update: GROUP_CREATE g3 bob`.
3. In **T2** type `quit`. T1 prints `promoting to PRIMARY`.
4. C2: `list_groups` (prints `Reconnected...`), then `list_groups` again → `media`, `g2`, `g3`.

✅ **Feature: failover works in both directions with no data loss.**

---

## 16. Leave group + owner hand-over

(After step 15 each client first prints `Reconnected. Please re-run your last command.`; just run the command again.)

| Client | Command | Expected |
|---|---|---|
| C3 | `leave_group media` | `OK carol left group media` |
| C3 | `list_files media` | `ERR You are not a member of group media` |
| C1 | `leave_group media` | `OK alice left group media` (alice was the owner) |
| C2 | `list_requests media` | `OK No pending requests for group media` (**bob is the new owner**) |
| C2 | `list_files media` | only the files bob shares: `bunny_5mb.mp4`, `document_10mb.pdf`, `jellyfish_10mb.mp4` |

✅ **Features: leave group, your shared files leave with you, ownership passes to the oldest remaining member.**

---

## 17. Exit

| Where | Command | Expected |
|---|---|---|
| any client | `exit` (also fine while logged in) or Ctrl+D | `[CLIENT] Exiting...`, clean exit, no crash |
| tracker | `quit` | `[TRACKER] Quitting` |

---

## Where things are saved

| What | Location |
|---|---|
| Downloaded files | the destination you give `download_file` (folder → original name, full path → that name) |
| `downloads.log`, `uploads.log` | the folder the client was started from (e.g. `~/p2ptest/bob/`) |
| Shared-file list per user | `~/.p2p_seeds_<user>.txt` |
| Tracker state | in memory only (lost when **both** trackers stop) |

---

## Quick automated smoke test (optional)

Uses the media files from step 2, spare ports (5600/5601, 7601/7602) and a temporary
folder, so it doesn't touch the manual test above. Prints four `OK` lines on success.

```bash
R=~/Desktop/P2P/Peer-To-Peer_File_Sharing
S=$(mktemp -d); cd $S; mkdir -p c1 c2 dl home
cp ~/p2ptest/alice/bunny_5mb.mp4 ~/p2ptest/alice/document_10mb.pdf c1/
printf '127.0.0.1 5600\n127.0.0.1 5601\n' > ti.txt
HOME=$S/home $R/tracker/tracker ti.txt 1 </dev/null > t1.log 2>&1 & T1=$!; sleep 0.5
HOME=$S/home $R/tracker/tracker ti.txt 2 </dev/null > t2.log 2>&1 & T2=$!; sleep 0.5
mkfifo f1 f2
(cd c1; HOME=$S/home $R/client/client 127.0.0.1:7601 ../ti.txt <../f1 >../c1.log 2>&1) &
(cd c2; HOME=$S/home $R/client/client 127.0.0.1:7602 ../ti.txt <../f2 >../c2.log 2>&1) &
exec 3>f1 4>f2; s(){ echo "$2" >&$1; sleep 0.5; }
s 3 "create_user alice pw"; s 3 "login alice pw"; s 3 "create_group media"
s 4 "create_user bob pw"; s 4 "login bob pw"; s 4 "join_group media"
s 3 "accept_request media bob"
s 3 "upload_file media bunny_5mb.mp4"; s 3 "upload_file media document_10mb.pdf"
s 4 "download_file media bunny_5mb.mp4 $S/dl"; sleep 2
s 4 "download_file media document_10mb.pdf $S/dl/renamed.pdf"; sleep 2
cmp c1/bunny_5mb.mp4 dl/bunny_5mb.mp4      && echo "video OK"
cmp c1/document_10mb.pdf dl/renamed.pdf    && echo "pdf OK"
kill $T1; sleep 2; s 4 "list_files media"
s 4 "download_file media bunny_5mb.mp4 $S/dl/after_failover.mp4"; sleep 2
cmp c1/bunny_5mb.mp4 dl/after_failover.mp4 && echo "download after failover OK"
s 3 "exit"; s 4 "exit"; exec 3>&- 4>&-; kill $T2
grep -q "Aborted\|terminate" c1.log c2.log || echo "clean exit OK"
echo "logs: $S/c1.log $S/c2.log"
```

---

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `Bind failed: Address already in use` (tracker) | An old tracker is still running: `pkill -f "tracker tracker_info"` |
| `Failed to start uploader server ... (port in use?)` | Another client already uses that IP:PORT. Pick another port. |
| `Could not connect to any PRIMARY tracker` | Start tracker 1 (and 2) first; check `tracker_info.txt`. |
| `Cannot create ~/...: No such file or directory` | You used `~` inside the client. Use `/home/nilkanta/...`. |
| `No online peer is sharing X right now` | Every owner of the file is logged out. Log one in. |
| Download failed | Details are in `downloads.log` in the client's folder. |

# CNT_Project2
## Introduction
Like so many other people, your professor is frustrated with the fact that every machine he owns has a different collection of songs in his music library. Students will solve this problem and develop UFmyMusic, a networked application that enables multiple machines to ensure that they have the same files in their music directory.

## To run
1. In one terminal run:
  ```sh
  make
  ```

2. Have as many terminals as needed open.
- To start the server run:
  ```sh
  ./runserver
  ```
- To connect the client to the server in the other terminals run
   ```sh
  ./runclient
  ```
- In client terminals, when prompted, choose an option (LIST, DIFF, PULL, LEAVE)

## Notes
1. Sometimes when running LIST only one file will show up then the others will come after. Just run LIST again and they should all show up

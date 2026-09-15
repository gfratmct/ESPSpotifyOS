package main

import (
	"fmt"
	"io"
	"os"

	"github.com/gfratmct/ESPSpotifyOS/service/internal/services"
)

func main() {
	ytdlpService := services.NewYouTubeDLPService("./storage")
	exists := ytdlpService.IsYtDlpInstalled()
	fmt.Printf("Is yt-dlp installed? %v\n", exists)
	results, err := ytdlpService.Search("gianni celeste ho litigato con mia moglie", 5)
	if err != nil {
		fmt.Printf("Search failed: %v\n", err)
	} else {
		fmt.Printf("Search results: %v\n", results)
	}

	// resulst must be greater than 0
	if len(results) == 0 {
		fmt.Println("No search results found")
		panic("No search results found")
	}

	// download the first search result
	mediaItem, err := ytdlpService.Download(results[0], "")
	if err != nil {
		fmt.Printf("Download failed: %v\n", err)
	} else {
		fmt.Printf("Downloaded media item: %v\n", mediaItem)
	}

	// stream mp3 media if downloaded
	if mediaItem.FilePath != "" {
		fmt.Printf("Streaming media from: %s\n", mediaItem.FilePath)
	}

	// open file
	stream, err := os.Open(mediaItem.FilePath)
	if err != nil {
		fmt.Printf("Failed to open file: %v\n", err)
		return
	}
	defer stream.Close()

	const chunkSize = 1024 // 1 KB buffer for streaming
	buffer := make([]byte, chunkSize)
	channel := make(chan []byte)
	for {
		n, err := stream.Read(buffer)
		if err != nil {
			if err != io.EOF {
				fmt.Printf("Error reading file: %v\n", err)
			}
			close(channel)
			break
		}
		if n > 0 {
			// process the chunk of data read from the file
			fmt.Printf("Read %d bytes\n", n)
			channel <- buffer[:n]
		}
	}

	// close the channel after streaming is done
	close(channel)
	// consume the remaining data from the channel
	for chunk := range channel {
		fmt.Printf("Received chunk of size: %d\n", len(chunk))
	}
}

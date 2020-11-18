package main

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
	"os"
	"os/exec"
	"os/signal"
	"sync"
	"syscall"
	"time"
	"net"

	"github.com/gin-gonic/gin"
)

const webBackendPort int = 8888
const configDir string = "/etc/fieldbus/"
const configPathName string = "/etc/fieldbus/config.json"

var (
	ctx    context.Context
	cancel context.CancelFunc

	fdctx    context.Context
	fdcancel context.CancelFunc
)

// TagEntry ...
type TagEntry struct {
	TagName string `json:"tagName"`
	//Opartion string `json:"op"`   // r/w, r, w
	//Type     string `json:"type"` // boolean, int, uint, float, double, string...
}

// DeviceConfiguration ...
type DeviceConfiguration struct {
	DeviceName      string     `json:"deviceName"`
	PollingInterval int        `json:"pollingInterval"`
	Tags            []TagEntry `json:"tags"`
}

type TagInfo struct {
    PrvdName string   `json:"prvdName"`  
    SrcName  string   `json:"srcName"`  
    TagName  string   `json:"tagName"`
    DataType string   `json:"dataType"`
    DataUnit string   `json:"dataUnit"`
    Access   string   `json:"access"`
}

type TagValue struct {
    Method       string   `json:"method"`
    DatabaseName string   `json:"databaseName"`
    TableName    string   `json:"tableName"`
    TagName      string   `json:"tagName"`
    TagValue     string   `json:"tagValue"`
}


var configuraiton DeviceConfiguration

//socket
var socketClientConn *net.TCPConn

// GetConfiguration return configuration
func GetConfiguration() DeviceConfiguration {
	return configuraiton
}




// GetTagValue ...
func GetTagValue(c *gin.Context) interface{} {

	method := c.Request.Method	
	if method != "PUT" {
		c.String(http.StatusBadRequest, "Requested method is not allowed")
		return nil
	}
	
	body := TagValue{Method:"get"}
	if err := c.ShouldBindJSON(&body); err != nil {
		c.String(http.StatusBadRequest, "Requested payload is invalid")
		return nil
	}
	
	if getTagValueInfo, err := json.Marshal(body); err == nil {

        if socketClientConn == nil {
        	c.String(http.StatusBadRequest, "socket connect error")
            return nil
        }
        
        socketClientConn.Write([]byte(getTagValueInfo))

        buff := [1024]byte{}
    	n, _ := socketClientConn.Read(buff[:])
    	var tag TagValue
    	json.Unmarshal([]byte(buff[:n]), &tag)
		return tag

	}

	c.String(http.StatusBadRequest, "Requested payload is invalid")
	return nil    
}

// ReadTagValue ...
func ReadTagValue(c *gin.Context) {
	c.JSON(http.StatusOK, gin.H{"data": GetTagValue(c)})
}



// SetTagValue ...
func SetTagValue(c *gin.Context) interface{} {
	method := c.Request.Method	
	if method != "PUT" {
		c.String(http.StatusBadRequest, "Requested method is not allowed")
		return nil
	}
	
	body := TagValue{Method:"set"}
	if err := c.ShouldBindJSON(&body); err != nil {
		c.String(http.StatusBadRequest, "Requested payload is invalid")
		return nil
	}
	
	if getTagValueInfo, err := json.Marshal(body); err == nil {

        if socketClientConn == nil {
        	c.String(http.StatusBadRequest, "socket connect error")
            return nil
        }
        
        socketClientConn.Write([]byte(getTagValueInfo))

        buff := [1024]byte{}
    	n, _ := socketClientConn.Read(buff[:])
    	var tag TagValue
    	json.Unmarshal([]byte(buff[:n]), &tag)
		return tag

	}

	c.String(http.StatusBadRequest, "Requested payload is invalid")
	return nil 

}

func WriteTagValue(c *gin.Context) {
	c.JSON(http.StatusOK, gin.H{"data": SetTagValue(c)})
}



// HandleConfig ...
func HandleConfig(c *gin.Context) interface{} {

	method := c.Request.Method
	if method == "GET" {
		return GetConfiguration()
	}
	if method != "POST" && method != "PUT" && method != "PATCH" {
		c.String(http.StatusBadRequest, "Requested method is not allowed")
		return nil
	}

	body := DeviceConfiguration{}
	if err := c.ShouldBindJSON(&body); err != nil {
		c.String(http.StatusBadRequest, "Requested payload is invalid")
		return nil
	}

	if putConfig, err := json.Marshal(body); err == nil {
		ioutil.WriteFile(configPathName, putConfig, os.ModeAppend)
		return GetConfiguration()
	}

	c.String(http.StatusBadRequest, "Requested payload is invalid")
	return nil
}

// AnyConfig ...
func AnyConfig(c *gin.Context) {
	c.JSON(http.StatusOK, gin.H{"data": HandleConfig(c)})
}

func startFieldbusDaemon() {
	ctx, cancel = context.WithCancel(context.TODO())
	fdctx, fdcancel = context.WithCancel(context.TODO())
	go func() {
		execcmd := exec.CommandContext(fdctx, "stdbuf", "-oL", "/usr/local/sbin/fbdaemon", "-f")
		stdout, err := execcmd.StdoutPipe()
		if err != nil {
			fmt.Printf("stdout pipe command failed\n")
			return
		}
		err = execcmd.Start()
		if err != nil {
			fmt.Printf("start command failed\n")
			return
		}

		go func() {
			defer stdout.Close()
			scanner := bufio.NewScanner(stdout)
			for scanner.Scan() {
				fmt.Printf("[fieldbus protocol daemon] %s\n", scanner.Text())
			}
		}()

		go func() {
			execcmd.Wait()
			fmt.Printf("fieldbus protocol daemon is quit\n")
			fdcancel()
		}()

		<-ctx.Done()
		execcmd.Process.Signal(syscall.SIGTERM)
	}()
}

func stopFieldbusDaemon() {
	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		cancel()
		select {
		case <-time.After(5 * time.Second):
			fdcancel()
		case <-fdctx.Done():
			break
		}
	}()
	wg.Wait()
}

// PutRestart ...
func PutRestart(c *gin.Context) {
	stopFieldbusDaemon()
	time.Sleep(1 * time.Second)
	startFieldbusDaemon()
}

func GetTags(c *gin.Context) interface{} {

	method := c.Request.Method
	if method != "GET" {
		c.String(http.StatusBadRequest, "Requested method is not allowed")
		return nil
	}


    count := len(configuraiton.Tags)
    slice := make([]TagInfo, count)

    for i, v := range configuraiton.Tags {

        slice[i].PrvdName = "fieldbusprotocol"
        slice[i].SrcName = configuraiton.DeviceName
        slice[i].TagName = v.TagName
        slice[i].DataType = "int16"
        slice[i].Access = "rw"
    }

    //fmt.Println(slice, count)
    return slice[:]

}

// GetAllTags ...
func GetAllTags(c *gin.Context) {
	c.JSON(http.StatusOK, gin.H{"data": GetTags(c)})
}




func main() {
	// create config directory
	if _, err := os.Stat(configDir); os.IsNotExist(err) {
		os.MkdirAll(configDir, os.ModePerm)
	}

	// init config
	str, _ := ioutil.ReadFile(configPathName)
	json.Unmarshal([]byte(str), &configuraiton)
	startFieldbusDaemon()
    socket_client()

	
	r := gin.Default()
	r.Any("/config", AnyConfig)
	r.GET("/tags", GetAllTags)
	r.PUT("/restart", PutRestart)
	r.PUT("operate/direct-read-tag", ReadTagValue)
	r.PUT("operate/direct-write-tag", WriteTagValue)
	r.Run(fmt.Sprintf(":%d", webBackendPort))
    
	errs := make(chan error, 1)
	listenSystemInterrupt(errs)
	c := <-errs
	fmt.Printf("fieldbus protocol controller quits(%v)\n", c)
}

func listenSystemInterrupt(errchan chan error) {
	go func() {
		c := make(chan os.Signal)
		signal.Notify(c, syscall.SIGINT)
		errchan <- fmt.Errorf("%s", <-c)
	}()
}

func socket_client() {

    
	go func() {
	    server := "127.0.0.1:8000"
        time.Sleep(time.Duration(2)*time.Second)
        tcpAddr, _ := net.ResolveTCPAddr("tcp", server)

        for {
            if socketClientConn == nil {
                socketClientConn, _ = net.DialTCP("tcp", nil, tcpAddr)
            } 
            time.Sleep(time.Duration(2)*time.Second)
        }
        
	}()
}


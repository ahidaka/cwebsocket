var http = require('http');
var clientHtml = require('fs').readFileSync('logger.html');
var plainHttpServer = http.createServer(function(req, res) {
	res.writeHead(200, { 'Content-Type': 'text/html'});
	res.end(clientHtml);
}).listen(8000);

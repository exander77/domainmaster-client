<?php
/**
 * Klient pro MasterAPI,
 * pomoci ktereho je mozne roboticky pristupovat k funkcim systemu www.DomainMaster.cz.
 * www.DomainMaster.cz provozuje spolecnost General Registry.
 *
 * Vice infomraci o MasterAPI naleznete na adrese:
 * http://www.domainmaster.cz/masterapi/
 */
class MasterApi_Client{
	public $_Username = "";
	public $_Password = "";

	public $_Charset = "UTF-8";

	public $_Host = "www.domainmaster.cz";
	public $_Uri = "/masterapi/server.php";
	public $_Port = 443;
	public $_Ssl = true;
	public $_DataFormat = "yaml"; // "yaml" nebo "json"

	/**
	 * Konstruktor.
	 *
	 *  $client = new MasterApi_Client(array(
	 *  	"username" => "GR:PLATCE",
	 *  	"password" => "secret",
	 *  	"charset" => "ISO-8859-2", // "UTF-8", "WINDOWS-1250", "ISO-8859-2",
	 *  	"server_url" => "https://www.domainmaster.cz/masterapi/server.php"
	 *  ));
	 *
	 * @access public
	 * @param array
	 */
	function __construct($params = []){
		$params = array_merge(["username" => "", "password" => "", "charset" => "UTF-8", "server_url" => "https://www.domainmaster.cz/masterapi/server.php", "data_format" => "yaml"], $params);

		$this->_Username = strtoupper($params["username"]);
		$this->_Password = $params["password"];
		$this->_Charset = $params["charset"];
		$this->_DataFormat = $params["data_format"];

		if(preg_match("/^http(s?):\\/\\/([^\\/:]+)(:(\\d{1,6})|)(\\/.*)\$/", (string) $params["server_url"], $matches)){
			$this->_Ssl = ($matches[1]=="s");
			$this->_Host = $matches[2];
			if(strlen((string) $matches[3])>0){
				$this->_Port = (int)$matches[4];
			}elseif($this->_Ssl){
				$this->_Port = 443;
			}else{
				$this->_Port = 80;
			}
			$this->_Uri = $matches[5];
		}
	}
	
	/**
	 * Odesle prikaz do Master API serveru.
	 *
	 *		// prikaz nevyzadujici autorizaci
	 *		$res = $client->sendCommand("register cz domain",array(
	 *			"domain" => "domenka.cz",
	 *			"registrant" => "ID-MAJITELE",
	 *			"admin" => array("ID-WEBHOSTER-1", "ID-WEBHOSTER-2"),
	 *			"idacc" => "GR:PLATCE"
	 *		));
	 *
	 *		// prikaz vyzadujici autorizaci opravnenou osobou
	 *		$res = $client->sendCommand("update cd domain",array(
	 *			"domain" => "domenka.cz",
	 *			"nsset" => "ID-NSSET",
	 *		),array(
	 *			"contact" => "ID-WEBHOSTER-1",
	 *			"password" => "heslo kontaktu ID-WEBHOSTER-1"
	 *		));
	 * 
	 * @access public
	 * @param string $command					ie. "credit info"
	 * @param array $params						parametry prikazu (jsou-li potreba)
	 * @param array $authorization			kontakt a heslo potvrzujici zmenu (je-li potreba)
	 * @return MasterApi_ClientResult
	 */
	function sendCommand($command,$params = [],$authorization = null){
		$ar = ["command" => $command, "params" => $params];

		if($authorization){
			$ar["authorization"] = array_merge(["contact" => "", "password" => "", "contact_type" => "auto"], $authorization);
		}

		$data = match ($this->_DataFormat) {
            "json" => json_encode($ar, JSON_THROW_ON_ERROR),
             default => miniYAML::Dump($ar),
        };

		$buff = [];
		$buff[] = "POST $this->_Uri HTTP/1.0";
		$buff[] = "Host: $this->_Host";
		$buff[] = "Content-Type: text/plain; charset=$this->_Charset";
		$buff[] = "Content-Length: ".strlen($data);
		$buff[] = "Authorization: Basic ".base64_encode("$this->_Username:$this->_Password");
		$buff[] = "";
		$buff[] = $data;
		$buff = implode("\n", $buff);
		
		$_ssl = "";
		if($this->_Ssl){
			$_ssl = "ssl://";
		}
		if(!($f = fsockopen("$_ssl$this->_Host", $this->_Port, $errno, $errstr))){
			return new MasterApi_ClientResult(["http_request" => $buff, "network_error" => "can't open socket: $errstr ($errno)"]);
		}
		if(!fwrite($f, $buff, strlen($buff))){
			return new MasterApi_ClientResult(["http_request" => $buff, "network_error" => "can't write to socket"]);
		}
		$response = "";
		while($f && !feof($f)){
			$response .= fread($f, 4096);
		}
		fclose($f);

		return new MasterApi_ClientResult(["http_request" => $buff, "http_response" => $response]);
	}
}

/**
 * Trida pro zapouzdreni vysledku volani MasterApi_Client::sendCommand().
 *
 */
class MasterApi_ClientResult{
	public $_HttpRequest = null;
	public $_HttpResponseHeaders = null;
	public $_HttpResponseContent = null;

	public $_Result = null;

	public $_NetworkError = null; 

	function __construct($params){
		$params = array_merge(["http_request" => null, "http_response" => null, "network_error" => null],$params);

		$this->_HttpRequest = $params["http_request"];
		$this->_NetworkError = $params["network_error"];

		if(preg_match("/^(.*?)\\r?\\n\\r?\\n(.*)$/s", (string) $params["http_response"], $matches)){
			$this->_HttpResponseHeaders = $matches[1];
			$this->_HttpResponseContent = $matches[2];
		}
		if(preg_match('/^\s*{/s', (string) $this->_HttpResponseContent)){ // autodetekce json
			$this->_Result = json_decode((string) $this->_HttpResponseContent, true, 512, JSON_THROW_ON_ERROR);
		}else{
			$this->_Result = miniYAML::Load($this->_HttpResponseContent);
		}
	}

	function isSuccess(){
		return (
			$this->_Result !== null &&
			$this->_Result["status"]=="success"
		);	
	}

	function isTemporaryError(){
		return (
			($this->_Result !== null && $this->_Result["status"]=="temporary error") ||
			($this->_NetworkError !== null) // nastane-li chyba na siti, jde o docasnou chybu
		);
	}

	function getMessage(){
		if($this->_NetworkError !== null){
			return $this->_NetworkError;
		}
		if($this->_Result === null){
			// pokud se nam podari vyzobnout HTTP response kode, vratime ho
			if(preg_match("/^HTTP\\/\\d.\\d (\\d{3}.*)/", (string) $this->_HttpResponseHeaders, $matches)){
				return "HTTP response code: $matches[1]";
			}
			return "response has not been successfuly parsed";
		}
		return $this->_Result["message"];
	}

	function getData(){
		if($this->_Result !== null && isset($this->_Result["data"])){
			return $this->_Result["data"];
		}
		return null;
	}

	function getHttpRequest(){
		return $this->_HttpRequest;
	}

	function getHttpResponse(){
		if($this->_HttpResponseHeaders !== null){
			return $this->_HttpResponseHeaders."\n\n".$this->_HttpResponseContent;
		}
		return null;
	}
}

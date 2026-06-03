var hostname = window.location.search!=""?window.location.search.substring(1):window.location.hostname;
var websocket;
var wserrcnt = 0;
var wstimeout;
var currentItem = 0;

window.addEventListener('load', onLoad);

function initWebSocket() {
  clearTimeout(wstimeout);
  console.log('Trying to open a WebSocket connection...');
  websocket = new WebSocket(`ws://${hostname}/ws`);
  websocket.onopen    = onOpen;
  websocket.onclose   = onClose;
  websocket.onmessage = onMessage;
}
function onOpen(event) {
  console.log('Connection opened');
  wserrcnt=0;
  websocket.send('getmode=1');
}
function onClose(event) {
  wserrcnt++;
  wstimeout=setTimeout(initWebSocket, wserrcnt<10?2000:120000);
}
function escapeData(data){
  let m=data.match(/{.+?:\s"(.+?)"}/);
  if(m!==null){
    let m1 = m[1];
    if(m1.indexOf('"') !== -1){
      let mq=m1.replace(/["]/g, '\\\"');
      return data.replace(m1,mq);
    }
  }
  return data;
}
function quoteattr(s) {
  return ('' + s)
    .replace(/&/g, '&amp;')
    .replace(/'/g, '&apos;')
    .replace(/"/g, '&quot;')
    .replace(/</g, '&lt;');
}
function secondToTime(seconds){
  if(seconds>=3600){
    return new Date(seconds * 1000).toISOString().substring(11, 19);
  }else{
    return new Date(seconds * 1000).toISOString().substring(14, 19);
  }
}
function onMessage(event) {
  var data = JSON.parse(escapeData(event.data));
  if(typeof data.pmode !== 'undefined'){
    if(data.pmode=="player"){
      if(window.location.pathname.includes("settings")){
        websocket.send('getsystem=1');
        websocket.send('getscreen=1');
        websocket.send('gettimezone=1');
        websocket.send('getweather=1');
        websocket.send('getai=1');
        websocket.send('getcontrols=1');
        getWiFi(`http://${hostname}/data/wifi.csv`+"?"+new Date().getTime());
        websocket.send('getactive=1');
      }else if(window.location.pathname.includes("index") || window.location.pathname=="/"){
        websocket.send('getindex=1');
        generatePlaylist(`http://${hostname}/data/playlist.csv`+"?"+new Date().getTime());
      }
    }else{
      getWiFi(`http://${hostname}/data/wifi.csv`+"?"+new Date().getTime());
      websocket.send('getactive=1');
    }
    return;
  }
  if(typeof data.sst !== 'undefined'){
    checkSelectSet("smartstart",data.sst);
    checkSelectSet("audioinfo",data.aif);
    checkSelectSet("vumeter",data.vu);
    if(document.getElementById('usespectrum')) updateUseSpectrumAvailability(data.vu==1);
    setSlRangeValue(document.getElementById("slsoftap"),data.softr);
    if(typeof data.mdns !== 'undefined') document.getElementById("mdnsname").value=data.mdns;
    return;
  }
  if(typeof data.flip !== 'undefined'){
    checkSelectSet("flipscreen",data.flip);
    checkSelectSet("invertdisplay",data.inv);
    checkSelectSet("numplaylist",data.nump);
    checkSelectSet("fliptouch",data.tsf);
    checkSelectSet("dbgtouch",data.tsd);
    checkSelectSet("screenon",data.dspon);
    setSlRangeValue(document.getElementById("slbrightness"),data.br);
    setSlRangeValue(document.getElementById("slcontrast"),data.con);
    if(typeof data.sa !== 'undefined' && document.getElementById('usespectrum')) checkSelectSet("usespectrum",data.sa);
    if(typeof data.scre !== 'undefined') checkSelectSet("screensaverenabled",data.scre);
    if(typeof data.scrt !== 'undefined') document.getElementById("screensavertimeout").value=data.scrt;
    if(typeof data.scrb !== 'undefined') checkSelectSet("screensaverblank",data.scrb);
    if(typeof data.scrpe !== 'undefined') checkSelectSet("screensaverplayingenabled",data.scrpe);
    if(typeof data.scrpt !== 'undefined') document.getElementById("screensaverplayingtimeout").value=data.scrpt;
    if(typeof data.scrpb !== 'undefined') checkSelectSet("screensaverplayingblank",data.scrpb);
    return;
  }
  if(typeof data.redirect !== 'undefined'){
    document.getElementById("mdnsnamerow").innerHTML=`<h3 style="line-height: 37px;color: #aaa; margin: 0 auto;">redirecting to ${data.redirect}</h3>`;
    setTimeout(function(){ window.location.href=data.redirect; }, 4000);
    return;
  }
  if(typeof data.dspontrue !== 'undefined'){
    checkSelectSet("screenon",data.dspontrue);
    return;
  }
  if(typeof data.tzh !== 'undefined'){
    document.getElementById("tzhour").value=data.tzh;
    document.getElementById("tzmin").value=data.tzm;
    document.getElementById("sntp2").value=data.sntp2;
    document.getElementById("sntp1").value=data.sntp1;
    return;
  }
  if(typeof data.wen !== 'undefined'){
    checkSelectSet("showweather",data.wen);
    document.getElementById("weatherlat").value=data.wlat;
    document.getElementById("weatherlon").value=data.wlon;
    document.getElementById("weatherkey").value=data.wkey;
    return;
  }
  if(typeof data.aien !== 'undefined'){
    checkSelectSet("ai_enabled", data.aien);
    document.getElementById("ai_host").value = data.aihost;
    document.getElementById("ai_port").value = data.aiport;
    document.getElementById("ai_path").value = data.aipath;
    document.getElementById("ai_timeout").value = data.aitimeout;
    document.getElementById("ai_model").value = data.aimodel;
    document.getElementById("ai_key").value = data.aikey;
    // Update prompt status / Обновить статус промпта
    if(typeof data.aiprompt_loaded !== 'undefined'){
      updatePromptStatus(data.aiprompt_loaded, data.aiprompt_size);
    }
    return;
  }
  if(typeof data.sdpos !== 'undefined'){
    //setSlRangeValue(document.getElementById("sdpos"),data.sdpos);
    if(data.sdtpos==0 && data.sdtend==0){
      document.getElementById("sdposvalscurrent").innerHTML="00:00";
      document.getElementById("sdposvalsend").innerHTML="00:00";
      setSlRangeValue(document.getElementById("sdpos"),0);
    }else{
      document.getElementById("sdposvalscurrent").innerHTML=secondToTime(data.sdtpos);
      document.getElementById("sdposvalsend").innerHTML=secondToTime(data.sdtend);
      setSlRangeValue(document.getElementById("sdpos"),data.sdpos);
    }
    return;
  }
  if(typeof data.sdmin !== 'undefined'){
    document.getElementById("sdpos").setAttribute('min',data.sdmin); 
    document.getElementById("sdpos").setAttribute('max',data.sdmax); 
    return;
  }
  if(typeof data.snuffle!== 'undefined'){
    if(data.snuffle==1){
      document.getElementById("snuffle").classList.add("active");
    }else{
      document.getElementById("snuffle").classList.remove("active");
    }
    return;
  }
  
  if(typeof data.vols !== 'undefined'){
    setSlRangeValue(document.getElementById("slvolsteps"),data.vols);
    setSlRangeValue(document.getElementById("slencacceleration"),data.enca);
    setSlRangeValue(document.getElementById("slirtlp"),data.irtl);
    if(typeof data.skipup !== 'undefined') checkSelectSet("oneclickswitching",data.skipup);
    return;
  }
  if(typeof data.act !== 'undefined'){
    let group = document.getElementsByClassName("group");
    for (var i = 0; i <= group.length - 1; i++) {
      for (let j = 0; j <= data.act.length-1; j++) {
        if(group[i].classList.contains(data.act[j])){
          group[i].classList.remove("hidden");
          break;
        }
      }
    }
    return;
  }
  if(document.getElementById('meta')){ // index loaded
    if(typeof data.nameset !== 'undefined') { document.getElementById('nameset').innerHTML = data.nameset; return; }
    if(typeof data.meta !== 'undefined') { document.getElementById('meta').innerHTML = data.meta; return; }
    if(typeof data.vol !== 'undefined') {
      setSlRangeValue(document.getElementById('volrange'),data.vol);
      return;
    }
    if(typeof data.current !== 'undefined') { setCurrentItem(data.current); return; }
    if(typeof data.file !== 'undefined') {  generatePlaylist(data.file+"?"+new Date().getTime()); websocket.send('submitplaylistdone=1'); return; }
    if(typeof data.bitrate !== 'undefined') { 
          let fmt=typeof data.format !== 'undefined'?data.format:'bitrate';
          document.getElementById('bitinfo').innerText = fmt+': '+data.bitrate+'kBits'; 
          return; 
        }
    if(typeof data.rssi !== 'undefined') {
      let rsiinfo=document.getElementById('rsiinfo');
      if(rsiinfo) rsiinfo.innerText = 'rssi: '+data.rssi+'dBm';
      return;
    }
    if(typeof data.mode !== 'undefined') { document.getElementById('playbutton').setAttribute("class",data.mode); return; }
    if(typeof data.bass !== 'undefined') {
    	setSlRangeValue(document.getElementById('eqbass'),data.bass);
    	setSlRangeValue(document.getElementById('eqmiddle'),data.middle);
    	setSlRangeValue(document.getElementById('eqtreble'),data.trebble);
    	return;
    }
    if(typeof data.balance !== 'undefined') {
      setSlRangeValue(document.getElementById('eqbal'),data.balance);
      return;
    }
    if(typeof data.playermode !== 'undefined') { //Web, SD
      document.getElementById('sdnav').classList.add('hidden');
      document.getElementById('volnav').classList.remove('hidden');
      document.getElementById('sdbutton').classList.remove('active');
      let elements = document.getElementsByClassName("modeitem");
      for (var i = 0; i < elements.length; i++) {
        elements[i].classList.remove('active');
      }
      document.getElementById(data.playermode).classList.add('active');
      document.body.setAttribute("class", data.playermode);
    }
    if(typeof data.sdinit !== 'undefined') {
      if(data.sdinit==1) {
        document.getElementById('equalizerwrap').classList.add("sd");
        document.getElementsByClassName('playerbyttonwrap')[0].classList.add("sd");
      }else{
        document.getElementById('equalizerwrap').classList.remove("sd");
        document.getElementsByClassName('playerbyttonwrap')[0].classList.remove("sd");
      }
    }
  }
}
function handleWiFiData(fileData) {
  if (!fileData) return;
  var lines = fileData.split('\n');
  for(var i = 0;i < lines.length;i++){
    let line = lines[i].split('\t');
    if(line.length==2){
      document.getElementById("ssid"+i).value=line[0].trim();
      document.getElementById("pass"+i).setAttribute('data-pass', line[1].trim());
    }
  }
}
function getWiFi(path){
  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (xhr.readyState == 4) {
      if (xhr.status == 200) {
        handleWiFiData(xhr.responseText);
      } else {
        handleWiFiData(null);
      }
    }
  };
  xhr.open("GET", path);
  xhr.send(null);
}
function submitWiFi(){
  var items=document.getElementsByClassName("credential");
  var output="";
  for (var i = 0; i <= items.length - 1; i++) {
    inputs=items[i].getElementsByTagName("input");
    if(inputs[0].value == "") continue;
    let ps=inputs[1].value==""?inputs[1].getAttribute('data-pass'):inputs[1].value;
    output+=inputs[0].value+"\t"+ps+"\n";
  }
  if(output!=""){ // Well, let's say, quack.
    let file = new File([output], "tempwifi.csv",{type:"text/plain;charset=utf-8", lastModified:new Date().getTime()});
    let container = new DataTransfer();
    container.items.add(file);
    let fileuploadinput=document.getElementById("file-upload");
    fileuploadinput.files = container.files;
    var formData = new FormData();
	  formData.append("wifile", fileuploadinput.files[0]);
	  var xhr = new XMLHttpRequest();
	  xhr.open("POST",`http://${hostname}/upload`,true);
	  xhr.send(formData);
	  fileuploadinput.value = '';
	  document.getElementById("settingscontent").innerHTML="<h2>Settings saved. Rebooting...</h2>";
	  document.getElementById("settingsdone").classList.add("hidden");
	  document.getElementById("navigation").classList.add("hidden");
	  setTimeout(function(){ window.location.href=`http://${hostname}/`; }, 10000);
  }
}
function scrollToCurrent(){
  var pl = document.getElementById('playlist');
  var lis = pl.getElementsByTagName('li');
  var plh = pl.offsetHeight;
  var plt = pl.offsetTop;
  var topPos = 0;
  var lih = 0;
  for (var i = 0; i <= lis.length - 1; i++) {
    if(i+1==currentItem) {
      topPos = lis[i].offsetTop;
      lih = lis[i].offsetHeight;
    }
  }
  pl.scrollTo({
    top: topPos-plt-plh/2+lih/2,
    left: 0,
    behavior: 'smooth'
  });
}
function setCurrentItem(item){
  currentItem=item;
  var pl = document.getElementById('playlist');
  var lis = pl.getElementsByTagName('li');
  for (var i = 0; i <= lis.length - 1; i++) {
    lis[i].removeAttribute('class');
    if(i+1==currentItem) {
      lis[i].setAttribute("class","active");
    }
  }
  scrollToCurrent();
}
function playStation(el){
  let lis = document.getElementById('playlist').getElementsByTagName('li');
  for (let i = 0; i <= lis.length - 1; i++) {
    lis[i].removeAttribute('class');
  }
  el.setAttribute("class","active");
  id=el.getAttribute('attr-id');
	xhr = new XMLHttpRequest();
	xhr.open("POST",`http://${hostname}/`,true);
	xhr.setRequestHeader("Content-type", "application/x-www-form-urlencoded");
	xhr.send("playstation="+id);
}
function handlePlaylistData(fileData) {
  if (!fileData) return;
  var lines = fileData.split('\n');
  var ul = document.getElementById('playlist');
  ul.innerHTML="";
  for(var i = 0;i < lines.length;i++){
    let line = lines[i].split('\t');
    if(line.length==3){
      li = document.createElement('li');
      li.setAttribute('onclick','playStation(this);');
      li.setAttribute('attr-id', i+1);
      li.setAttribute('attr-name', line[0].trim());
      li.setAttribute('attr-url', line[1].trim());
      li.setAttribute('attr-ovol', line[2].trim());
      if(i+1==currentItem){
        li.setAttribute("class","active");
      }
      var span = document.createElement('span');
      span.innerHTML = i+1;
      li.appendChild(document.createTextNode(line[0].trim()));
      li.appendChild(span);
      ul.appendChild(li);
    }
  }
  initPLEditor();
  scrollToCurrent();
}
function generatePlaylist(path){
  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (xhr.readyState == 4) {
      if (xhr.status == 200) {
        handlePlaylistData(xhr.responseText);
      } else {
        handlePlaylistData(null);
      }
    }
  };
  xhr.open("GET", path);
  xhr.send(null);
}
function checkSelectSet(name, val){
  let checkb=document.getElementById(name);
  checkb.classList.remove("on");
  checkb.classList.remove("off");
  checkb.classList.add(val==1?"on":"off");
}
function applyTZ(){
  websocket.send("tzh="+document.getElementById("tzhour").value);
  websocket.send("tzm="+document.getElementById("tzmin").value);
  websocket.send("sntp2="+document.getElementById("sntp2").value);
  websocket.send("sntp1="+document.getElementById("sntp1").value);
}
function checkSelect(){
  var checked = this.classList.contains("on");
  let newval;
  if(checked){
    this.classList.remove("on");
    this.classList.add("off");
    newval=0;
  }else{
    this.classList.remove("off");
    this.classList.add("on");
    newval=1;
  }
  websocket.send(this.getAttribute('id')+"="+newval);
  if(this.id==='vumeter'){
    updateUseSpectrumAvailability(newval==1);
  }
}
function updateUseSpectrumAvailability(vuOn){
  let el = document.getElementById('usespectrum');
  if(!el) return;
  if(!vuOn){
    el.classList.remove('on');
    el.classList.add('off');
    el.setAttribute('data-disabled','1');
  }else{
    el.removeAttribute('data-disabled');
  }
}
function checkSelectSA(){
  // Legacy WebUI rule: spectrum toggle only when VU is on (future LVGL may decouple).
  let vu = document.getElementById('vumeter');
  if(!vu || !vu.classList.contains('on')) return;
  var checked = this.classList.contains('on');
  let newval;
  if(checked){
    this.classList.remove('on');
    this.classList.add('off');
    newval=0;
  }else{
    this.classList.remove('off');
    this.classList.add('on');
    newval=1;
  }
  websocket.send(this.getAttribute('id')+"="+newval);
}
function initPLEditor(){
  ple= document.getElementById('pleditorcontent');
  ple.innerHTML="";
  pllines = document.getElementById('playlist').getElementsByTagName('li');
  for (let i = 0; i <= pllines.length - 1; i++) {
    plitem = document.createElement('li');
    plitem.setAttribute('class', 'pleitem');
    plitem.setAttribute('id', 'plitem'+i);
    let pName = pllines[i].getAttribute('attr-name');
    let pUrl = pllines[i].getAttribute('attr-url');
    let pOvol = pllines[i].getAttribute('attr-ovol');
    plitem.innerHTML = '<span class="grabbable" draggable="true">'+("00"+(i+1)).slice(-3)+'</span>\
          <span class="pleinput plecheck"><input type="checkbox" class="plcb" /></span>\
          <input class="pleinput plename" type="text" value="'+quoteattr(pName)+'" maxlength="140" />\
          <input class="pleinput pleurl" type="text" value="'+pUrl+'" maxlength="140" />\
          <input class="pleinput pleovol" type="number" min="-64" max="64" step="1" value="'+pOvol+'" />';
    ple.appendChild(plitem);
  }
}
function showEqualizer(){
	document.getElementById('equalizerbg').classList.toggle('hidden');
}
function selectAll(el){
  var cbxs = document.getElementsByClassName('plcb');
	for (var i = 0; i <= cbxs.length - 1; i++) {
		cbxs[i].checked = el.checked;
	}
}
function doPlUpload(finput) {
  websocket.send("submitplaylist=1");
  var formData = new FormData();
	formData.append("plfile", finput.files[0]);
	var xhr = new XMLHttpRequest();
	xhr.open("POST",`http://${hostname}/upload`,true);
	xhr.send(formData);
	finput.value = '';
}
function plAdd(){
	let ple=document.getElementById('pleditorcontent');
	let plitem = document.createElement('li');
	let cnt=ple.getElementsByTagName('li');
  plitem.setAttribute('class', 'pleitem');
  plitem.setAttribute('id', 'plitem'+(cnt.length));
  plitem.innerHTML = '<span class="grabbable" draggable="true">'+("00"+(cnt.length+1)).slice(-3)+'</span>\
      <span class="pleinput plecheck"><input type="checkbox" /></span>\
      <input class="pleinput plename" type="text" value="" maxlength="140" />\
      <input class="pleinput pleurl" type="text" value="" maxlength="140" />\
      <input class="pleinput pleovol" type="number" min="-30" max="30" step="1" value="0" />';
  ple.appendChild(plitem);
  ple.scrollTo({
		top: ple.scrollHeight,
		left: 0,
		behavior: 'smooth'
  });
}
function plRemove(){
	let items=document.getElementById('pleditorcontent').getElementsByTagName('li');
	let pass=[];
	for (let i = 0; i <= items.length - 1; i++) {
		if(items[i].getElementsByTagName('span')[1].getElementsByTagName('input')[0].checked) {
			pass.push(items[i]);
		}
	}
	if(pass.length==0) {
		alert('Choose something first');
		return;
	}
	for (var i = 0; i < pass.length; i++)
	{
		pass[i].remove();
	}
	items=document.getElementById('pleditorcontent').getElementsByTagName('li');
	for (let i = 0; i <= items.length-1; i++) {
		items[i].getElementsByTagName('span')[0].innerText=("00"+(i+1)).slice(-3);
	}
}
function submitPlaylist(){
  var items=document.getElementById("pleditorcontent").getElementsByTagName("li");
  var output="";
  for (var i = 0; i <= items.length - 1; i++) {
    inputs=items[i].getElementsByTagName("input");
    if(inputs[1].value == "" || inputs[2].value == "") continue;
    let ovol = inputs[3].value;
    if(ovol < -30) ovol = -30;
    if(ovol > 30) ovol = 30;
    output+=inputs[1].value+"\t"+inputs[2].value+"\t"+ovol+"\n";
  }
  let file = new File([output], "tempplaylist.csv",{type:"text/plain;charset=utf-8", lastModified:new Date().getTime()});
  let container = new DataTransfer();
  container.items.add(file);
  let fileuploadinput=document.getElementById("file-upload");
  fileuploadinput.files = container.files;
  doPlUpload(fileuploadinput);
  document.getElementById('pleditorwrap').classList.toggle('hidden');
}
function showEditor(){
  document.getElementById('equalizerbg').setAttribute('class','hidden');
  initPLEditor();
  document.getElementById('pleditorwrap').classList.toggle('hidden');
}
function playercommand(cmd){
	xhr = new XMLHttpRequest();
	xhr.open("POST",`http://${hostname}/`,true);
	xhr.setRequestHeader("Content-type", "application/x-www-form-urlencoded");
	xhr.send(cmd+"=1");
}
function playbutton(){
  var btn=document.getElementById('playbutton');
	if(btn.getAttribute("class")=="stopped") {
	  btn.setAttribute("class", "playing");
	  playercommand("start");
	  return;
	}
	if(btn.getAttribute("class")=="playing") {
	  btn.setAttribute("class", "stopped");
	  playercommand("stop");
	}
}
function changeMode(){
  if(this.classList.contains('active')) return;
  this.classList.add('active');
  websocket.send("newmode="+(this.getAttribute('data-name')=="web"?0:1));
}
function buttonClick(){
  let target=this.getAttribute('data-name');
  switch (target) {
    case "applyweather":
      let key=document.getElementById("weatherkey").value;
      if(key!=""){
        websocket.send("lat="+document.getElementById("weatherlat").value);
        websocket.send("lon="+document.getElementById("weatherlon").value);
        websocket.send("key="+key);
      }
      break;
    case "applyai":
      websocket.send("ai_enabled="+(document.getElementById("ai_enabled").classList.contains("on")?1:0));
      websocket.send("ai_host="+document.getElementById("ai_host").value);
      websocket.send("ai_port="+document.getElementById("ai_port").value);
      websocket.send("ai_path="+document.getElementById("ai_path").value);
      websocket.send("ai_timeout="+document.getElementById("ai_timeout").value);
      websocket.send("ai_model="+document.getElementById("ai_model").value);
      websocket.send("ai_key="+document.getElementById("ai_key").value);
      // Refresh AI status after apply / Обновить статус AI после применения
      websocket.send("getai=1");
      break;
    case "uploadprompt":
      uploadPromptFile();
      break;
    case "rebootmdns": websocket.send("rebootmdns="); break;
    case "fwupdate": window.location.href=`http://${hostname}/update`; break;
    case "wifiexport": window.open(`http://${hostname}/data/wifi.csv`+"?"+new Date().getTime()); break;
    case "wifiupload": submitWiFi(); break;
    case "setupir": window.location.href=`http://${hostname}/ir`; break;
    case "settingsdone": window.location.href=`http://${hostname}/`; break;
    case "equalizer": showEqualizer(); break;
    case "playlist": showEditor(); break;
    case "settings": window.location.href=`http://${hostname}/settings`; break;
    case "appearance": window.location.href=`http://${hostname}/appearance`; break;
    case "appearancedone": window.location.href=`http://${hostname}/`; break;
    case "prev": case "next": case "volp": case "volm": playercommand(target); break;
    case "play": playbutton(); break;
    case "plimport": break;
    case "plexport": window.open(`http://${hostname}/data/playlist.csv`); break;
    case "pladd": plAdd(); break;
    case "pldel": plRemove(); break;
    case "plsubmit": submitPlaylist(); break;
    case "sdcard": toggleSDCard(); break;
    case "snuffle": toggleSnuffle(); break;
    case "webboard": window.location.href=`http://${hostname}/webboard`; break;
    default: break;
  }
}
function toggleSnuffle(){
  document.getElementById('snuffle').classList.toggle('active');
  websocket.send("snuffle="+document.getElementById("snuffle").classList.contains('active'));
}
function toggleSDCard(){
  let sdbutton = document.getElementById('sdbutton');
  sdbutton.classList.toggle('active');
  if(sdbutton.classList.contains('active')){
    document.getElementById('sdnav').classList.remove('hidden');
    document.getElementById('volnav').classList.add('hidden');
  }else{
    document.getElementById('sdnav').classList.add('hidden');
    document.getElementById('volnav').classList.remove('hidden');
  }
}
function resetClick(){
  websocket.send("reset="+this.getAttribute('data-name'));
}
function navitemClick(){
  document.getElementById(this.getAttribute('data-target')).scrollIntoView({
      behavior: 'smooth'
  });
}
function onSlRangeChange(){
  websocket.send(this.name+"="+this.value);
}
function setSlRangeValue(el, val=null){
	let slave = el.getAttribute('data-slaveid');
  if(val!==null){
    el.value = val;
    if(slave!==null) document.getElementById(slave).innerText=val;
  }
  if(slave!==null) document.getElementById(slave).innerText=el.value;
  var value = (el.value-el.min)/(el.max-el.min)*100;
  el.style.background = 'linear-gradient(to right, #bfa73e 0%, #bfa73e ' + value + '%, #272727 ' + value + '%, #272727 100%)';
}
function initSliders(){
	var sliders = document.getElementsByClassName('slider');
	for (var i = 0; i <= sliders.length - 1; i++) {
		sliders[i].oninput = function() {
		  setSlRangeValue(this);
		};
		sliders[i].addEventListener('change', onSlRangeChange, false);
		setSlRangeValue(sliders[i], 0);
	}
	let sltemp = document.getElementById("volrange");
	if(sltemp) sltemp.addEventListener("wheel", function(e){
    if (e.deltaY < 0){
      this.valueAsNumber += 1;
    }else{
      this.value -= 1;
    }
    websocket.send('volume='+this.value);
    e.preventDefault();
    e.stopPropagation();
  }, {passive: false});
  
  let elements = document.getElementsByClassName("inputchange");
  for (var i = 0; i < elements.length; i++) {
    elements[i].addEventListener('input', onSlRangeChange, false);
  }
}
function initCheckboxes(){
  let elements = document.getElementsByClassName("checkbox");
  for (var i = 0; i < elements.length; i++) {
    if(elements[i].id==='usespectrum'){
      elements[i].addEventListener('click', checkSelectSA, false);
    }else{
      elements[i].addEventListener('click', checkSelect, false);
    }
  }
}
function initButtons(){
  let elements = document.getElementsByClassName("button");
  for (var i = 0; i < elements.length; i++) {
    elements[i].addEventListener('click', buttonClick, false);
  }
  elements = document.getElementsByClassName("playerbytton");
  for (var i = 0; i < elements.length; i++) {
    elements[i].addEventListener('click', buttonClick, false);
  }
  elements = document.getElementsByClassName("modeitem");
  for (var i = 0; i < elements.length; i++) {
    elements[i].addEventListener('click', changeMode, false);
  }
  elements = document.getElementsByClassName("reset");
  for (var i = 0; i < elements.length; i++) {
    elements[i].addEventListener('click', resetClick, false);
    elements[i].setAttribute("title", "reset "+elements[i].getAttribute('data-name')+" values");
  }
  elements = document.getElementsByClassName("navitem");
  for (var i = 0; i < elements.length; i++) {
    elements[i].addEventListener('click', navitemClick, false);
  }
  let applytz = document.getElementById("applytz");
  if(applytz) applytz.addEventListener('click', applyTZ, false);
}
function updatePromptStatus(loaded, size) {
  var statusEl = document.getElementById("ai_prompt_status");
  if(!statusEl) return;
  if(loaded == 1) {
    statusEl.innerText = "LOADED" + (size ? " (" + size + " bytes)" : "");
    statusEl.style.color = "#bfa73e";
  } else {
    statusEl.innerText = "NOT LOADED";
    statusEl.style.color = "#ccc";
  }
}
function uploadPromptFile() {
  var fileInput = document.getElementById("ai_prompt_file");
  if(!fileInput || !fileInput.files || fileInput.files.length == 0) {
    fileInput.click();
    return;
  }
  var file = fileInput.files[0];
  if(!file) {
    alert('Please select a file first');
    return;
  }
  // Check file extension / Проверка расширения файла
  if(!file.name.toLowerCase().endsWith('.txt')) {
    alert('Please select a .txt file');
    fileInput.value = '';
    return;
  }
  // Rename file to ai_prompt.txt for upload / Переименовать файл в ai_prompt.txt для загрузки
  var renamedFile = new File([file], "ai_prompt.txt", {type: "text/plain", lastModified: file.lastModified});
  var formData = new FormData();
  formData.append("file", renamedFile);
  var xhr = new XMLHttpRequest();
  var statusEl = document.getElementById("ai_prompt_status");
  if(statusEl) {
    statusEl.innerText = "Uploading...";
    statusEl.style.color = "#bfa73e";
  }
  xhr.onreadystatechange = function() {
    if (xhr.readyState == XMLHttpRequest.DONE) {
      if (xhr.status == 200) {
        if(statusEl) {
          statusEl.innerText = "Uploaded, refreshing...";
        }
        // Refresh AI status / Обновить статус AI
        setTimeout(function() {
          websocket.send("getai=1");
        }, 500);
      } else {
        if(statusEl) {
          statusEl.innerText = "Upload failed";
          statusEl.style.color = "#f00";
        }
        alert("Upload failed: " + (xhr.responseText || "Unknown error"));
      }
      fileInput.value = '';
    }
  };
  xhr.open("POST", `http://${hostname}/webboard`, true);
  xhr.send(formData);
}
function onLoad(event) {
  initCheckboxes();
  initSliders();
  initButtons();
  initWebSocket();
  // Initialize prompt upload button / Инициализировать кнопку загрузки промпта
  var uploadBtn = document.getElementById("ai_prompt_upload_btn");
  var fileInput = document.getElementById("ai_prompt_file");
  if(uploadBtn && fileInput) {
    uploadBtn.addEventListener('click', function() {
      if(fileInput.files && fileInput.files.length > 0) {
        uploadPromptFile();
      } else {
        fileInput.click();
      }
    }, false);
    fileInput.addEventListener('change', function() {
      if(this.files && this.files.length > 0) {
        uploadPromptFile();
      }
    }, false);
  }
}
/** UPDATE **/
var uploadWithError = false;
function doUpdate(el) {
  let binfile = document.getElementById('binfile').files[0];
  if(binfile){
    document.getElementById('updateform').setAttribute('class','hidden');
    document.getElementById("updateprogress").value = 0;
    document.getElementById('updateprogress').hidden=false;

    document.getElementById('update_cancel_button').hidden=true;
    var formData = new FormData();
    formData.append("updatetarget", document.getElementById('uploadtype1').checked?"firmware":"spiffs");
	  formData.append("update", binfile);
	  var xhr = new XMLHttpRequest();
	  uploadWithError = false;
	  xhr.onreadystatechange = function() {
      if (xhr.readyState == XMLHttpRequest.DONE) {
        if(xhr.responseText!="OK"){
          document.getElementById("uploadstatus").innerHTML = xhr.responseText;
          uploadWithError=true;
        }
      }
    }
	  xhr.upload.addEventListener("progress", progressHandler, false);
    xhr.addEventListener("load", completeHandler, false);
    xhr.addEventListener("error", errorHandler, false);
    xhr.addEventListener("abort", abortHandler, false);
	  xhr.open("POST",`http://${hostname}/update`,true);
	  xhr.send(formData);
	}else{
	  alert('Choose something first');
	}
}
function progressHandler(event) {
  var percent = (event.loaded / event.total) * 100;
  document.getElementById("uploadstatus").innerHTML = Math.round(percent) + "%&nbsp;&nbsp;uploaded&nbsp;&nbsp;|&nbsp;&nbsp;please wait...";
  document.getElementById("updateprogress").value = Math.round(percent);
  if (percent >= 100) {
    document.getElementById("uploadstatus").innerHTML = "Please wait, writing file to filesystem";
  }
}
var tickcount=0;
function rebootingProgress(){
  document.getElementById("updateprogress").value = Math.round(tickcount/7);
  tickcount+=14;
  if(tickcount>700){
    location.href=`http://${hostname}/`;
  }else{
    setTimeout(rebootingProgress, 200);
  }
}
function completeHandler(event) {
  if(uploadWithError) return;
  document.getElementById("uploadstatus").innerHTML = "Upload Complete, rebooting...";
  rebootingProgress();
}
function errorHandler(event) {
  document.getElementById('updateform').setAttribute('class','');
  document.getElementById('updateprogress').hidden=true;
  document.getElementById("updateprogress").value = 0;
  document.getElementById("status").innerHTML = "Upload Failed";
}
function abortHandler(event) {
  document.getElementById('updateform').setAttribute('class','');
  document.getElementById('updateprogress').hidden=true;
  document.getElementById("updateprogress").value = 0;
  document.getElementById("status").innerHTML = "Upload Aborted";
}
/** UPDATE **/

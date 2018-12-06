$(function(){

    // handle custom file inputs
    $("input[type=file]").change(function (event) {
        var fieldVal = event.target.files[0].name;
        if (fieldVal != undefined || fieldVal != "") {
            $(this).next(".custom-file-label").attr('data-content', fieldVal);
        }
    });

    $.getJSON('netif', function(data){
        var html = '<option value="">Select IP</option>';
        var len = data.length;
        for(var i = 0; i< len; i++) {
            html += '<option value="' + data[i].Gateway + '">' + data[i].Gateway + '</option>';
        }
        $('#netifModalSelect').html(html);
    });

    $('#netifModal').modal('show');
    $('#netifModalConnect').on("click", function(){
        $.ajax({
            type: "POST",
            url: "connect",
            data: $("#netifModalForm").serialize(), // serializes the form's elements.
            success: function(data)
            {
                $('#netifModal').modal('hide');
                setTimeout(function(){ draw(); }, 500);
            }
        });
    });

    $("#broadcast").on("click", function(e) {
        $("#firmware").prop('disabled', !this.checked);
        $("#updateFirmware").removeClass("disabled");
        $("#otaButton").removeClass("disabled");
        if (this.checked) {
            updateFirmware();
        } else {
            $("#updateFirmware").addClass("disabled");
            $("#otaButton").addClass("disabled");
            removeFirmware();
        }
    });

    $("#updateFirmware").on('click', updateFirmware);

    function removeFirmware() {
        var myNode = document.getElementById("firmware");
        while (myNode.firstChild) {
            myNode.removeChild(myNode.firstChild);
        }
    }
    function updateFirmware() {
        removeFirmware();
        $.getJSON('fw', function(data){
            var html = '<option value="">Select file</option>';
            var len = data.length;
            for(var i = 0; i< len; i++) {
                html += '<option value="' + data[i] + '">' + data[i] + '</option>';
            }
            $('#firmware').append(html);
        });
    } 

    $('#refresh').on("click", function() {
        update();
    });

    var updateView;
    var otaDone, setValues = false;
    $("#otaModal").on('hidden.bs.modal', function(e) {
        setValues = otaDone = false;
        $("#otaModal .modal-footer").attr('hidden', true);
        $('#connections').empty();
    });

    var temp_conn = $("#template-connection").html();
    var temp_broad = $("#template-connection-broadcast").html();
    var temp_error = $("#template-connection-error").html();
    Mustache.parse(temp_conn);
    Mustache.parse(temp_broad);
    Mustache.parse(temp_error);
    $('#otaButton').on("click", function(){
        $.ajax({
            type: "POST",
            url: "json",
            data: $("#otaForm").serialize(), // serializes the form's elements.
            success: function(data) {
                $("#otaModal").modal('show');
                
                updateView = setInterval(function () {
                    if (otaDone) {
                        $("#otaModal .modal-footer").attr('hidden', false);
                        clearInterval(updateView);
                    } else {
                        $.getJSON('otaInfo', function(data) {
                            if (data["error"] == undefined) {
                                var broadcast = data["type"] == 1;
                                var p = Math.max(0, Math.min(100, Math.ceil(data['progress'])));
                                if (setValues) {
                                    var obj = $("#connection");
                                    setProgress(obj, p);
                                    if (p == 100) {
                                        $(".progress-bar", obj).addClass("bg-success");
                                        otaDone = true;;
                                    }
                                } else {
                                    setValues = true;
                                    if (!broadcast) {
                                        $('#connections').html(Mustache.render(temp_conn, {name: data['id'], done: p}))
                                    } else {
                                        $('#connections').html(Mustache.render(temp_broad, {done: p}))
                                    }
                                }
                                if (data["node_error"] != undefined) {
                                    for(var i = 0; i < data["node_error"].length; i++) {
                                        $('#errors').append(Mustache.render(temp_error, {name: data["node_error"][i]}))
                                    }
                                    if (!broadcast) {
                                        $(".progress-bar", obj).addClass("bg-danger");
                                        otaDone = true;;
                                    }
                                }
                            }
                            
                        });
                    }
                }, 50);
            }
        });
    });


    function setProgress(obj, val) {
        $('.progress-bar', obj).css('width', val + '%').attr('aria-valuenow', val).text(val + '%');
    }











    var nodes, edges, network;

    // convenience method to stringify a JSON object
    function toJSON(obj) {
        return JSON.stringify(obj, null, 4);
    }

    var timeout;

    function update() {
        $.getJSON("/mesh", function(data){
            nodes.update(data.nodes);
            edges.update(data.edges);
            var removed = false;
            for (var i = 0; i < nodes.getIds().length; i++) {
                var id = nodes.getIds()[i];
                var remove = true;
                for(var j = 0; j < data.nodes.length; j++) {
                    var obj = data.nodes[j];
                    if(obj.id === id) {
                        remove = false;
                    }
                }
                if (remove) {
                    nodes.remove(id);
                    removed = true;
                }
            }
            if (removed) {
                network.fit();
            }
            $("#connectedAmount").text(nodes.getIds().length);
            timeout = setTimeout(function(){ update();}, 500);
        });
    }

    function draw() {
        
        var optionsIO = {
            groups: {
                pc: {
                    shape: 'icon',
                    icon: {
                        face: 'Ionicons',
                        code: '\uf380',
                        size: 50,
                        color: '#57169a'
                    }
                },
                node: {
                    shape: 'icon',
                    icon: {
                        face: 'Ionicons',
                        code: '\uf25c',
                        size: 50,
                        color: '#aa00ff'
                    }
                }
            }
        };
        // create an array with nodes
        nodes = new vis.DataSet();

        // create an array with edges
        edges = new vis.DataSet();

        $.getJSON("/mesh", function(data){
            update();
        });
        
        // create a network
        var container = document.getElementById('network');
        var data = {
            nodes: nodes,
            edges: edges
        };
        //var options = {};
        network = new vis.Network(container, data, optionsIO);

        
        network.on("click", function (params) {
            var clickedNode = nodes.get(this.getNodeAt(params.pointer.DOM));
            var label = clickedNode.label;
            var version = clickedNode.version;

            if (label === "PC") {
                label += " (this)";
                version = "NaN";
            }
            $('#eventSpan').html(
                    "<h1>" + label + "</h1>" +
                    "<h3>Version: <b>" + version + "</b></h3>" +
                    "<h3>Edges: <b>" + params.edges.length + "</b></h3>");
            
            if(clickedNode.group == "node" && label != undefined) {
                $('#nodeId').val(clickedNode.label);
                $.getJSON('fw', function(data){
                    removeFirmware();
                    var html = '<option value="">Select file</option>';
                    var len = data.length;
                    $("#broadcast").prop('disabled', true).prop('checked', false);
                    $("#firmware").prop('disabled', false);
                    $("#updateFirmware").removeClass("disabled");
                    $("#otaButton").removeClass("disabled");
                    for(var i = 0; i< len; i++) {
                        html += '<option value="' + data[i] + '">' + data[i] + '</option>';
                    }
                    $('#firmware').append(html);
                });
            } else {
                removeFirmware();

                if (label === undefined) {
                    var myNode = document.getElementById("eventSpan");
                    while (myNode.firstChild) {
                        myNode.removeChild(myNode.firstChild);
                    }
                }
                $('#nodeId').val("");
                $("#firmware").prop('disabled', true);
                $("#broadcast").prop('disabled', false);
                $("#updateFirmware").addClass("disabled");
                $("#otaButton").addClass("disabled");
            }
        });
    }
});
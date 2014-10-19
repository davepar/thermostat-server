/*
*  Javascript for displaying a temperature and humidity graph for a thermostat.
*  https://github.com/Davepar/thermostat-server
*/

var myApp = angular.module('Thermostat', []);

myApp.controller('ThermostatCtrl',
    ['$scope', '$http', function($scope, $http) {
  $scope.info = info_from_server;

  $scope.debug = function() {
    console.log($scope);
  };

  $scope.updateSchedule = function() {
    var params = {
      id: $scope.info.id,
      scheduleId: $scope.info.scheduleId
    };
    $http.post('/update', params)
      .success(function(data) {

      })
      .error(function(data) {

      });
  };

  // Distribute values using iterative approach to make it simpler.
  function distribute(vals, keys, delta) {
    function compare(a, b) {
      return vals[a] - vals[b];
    }
    keys.sort(compare);
    var distributed = false;
    while (!distributed) {
      distributed = true;
      for (var idx = 1; idx < keys.length; idx++) {
        var val1 = vals[keys[idx - 1]];
        var val2 = vals[keys[idx]];
        if (val2 - val1 < delta) {
          distributed = false;
          var ave = (val1 + val2) / 2;
          var spread = delta * 0.51;
          vals[keys[idx - 1]] = ave - spread;
          vals[keys[idx]] = ave + spread;
        }
      }
    }
  }

  // Draw the graph
  var margin = {top: 20, right: 90, bottom: 30, left: 50},
      width = 960 - margin.left - margin.right,
      height = 500 - margin.top - margin.bottom;

  var parseTime = d3.time.format.utc('%Y-%m-%d %H:%M:%S').parse;

  var x = d3.time.scale()
      .range([0, width]);

  var y = d3.scale.linear()
      .range([height, 0]);

  var color = d3.scale.category10();

  var xAxis = d3.svg.axis()
      .scale(x)
      .orient('bottom');

  var yAxis = d3.svg.axis()
      .scale(y)
      .orient('left')
      .ticks(5);

  var line = d3.svg.line()
      .x(function(d) { return x(d.time); })
      .y(function(d) { return y(d.value); });

  var svg = d3.select('#graph').append('svg')
      .attr('width', width + margin.left + margin.right)
      .attr('height', height + margin.top + margin.bottom)
    .append('g')
      .attr('transform', 'translate(' + margin.left + ',' + margin.top + ')');

  // // Catch 404 error
  // var title = 'Error: "' + json.because + '" for ID: ' + dweetId;
  // if (json.with === 404) {
  //   svg.append('text')
  //       .attr('x', (width / 2))
  //       .attr('y', 0)
  //       .attr('text-anchor', 'middle')
  //       .style('font-size', '16px')
  //       .text(title);
  //   return;
  // }

  // Map data into correct structure for D3
  var temps = [];
  var hums = [];
  var setTemps = [];
  var minValue = 100;
  var maxValue = 0;

  // Data is injected into html by server script
  if ('data' in $scope.info) {
    $scope.info.data.forEach(function(d) {
      var time = parseTime(d[0]);
      var temp = +d[1] / 10;
      var hum = +d[2] / 10;
      var setTemp = +d[3] / 10;

      temps.push({
        time: time,
        value: temp
      });
      hums.push({
        time: time,
        value: hum
      })
      setTemps.push({
        time: time,
        value: setTemp
      })
      minValue = Math.min(minValue, temp, hum, setTemp);
      maxValue = Math.max(maxValue, temp, hum, setTemp);
    });
  }

  var lastValue = null;
  if (temps[0]) {
    lastValue = {
      'temp': temps[0].value,
      'hum': hums[0].value,
      'set': setTemps[0].value,
      'time': temps[0].time
    };
  } else {
    lastValue = {
      'temp': '',
      'hum': '',
      'set': '',
      'time': ''
    };
  }
  var labels = {
    temp: 'Temperature: ' + lastValue['temp'] + '°F',
    hum: 'Humidity: ' + lastValue['hum'] + '%',
    setTemp: 'Set temp: ' + lastValue['set'] + '°F'
  };
  color.domain([labels.temp, labels.hum, labels.setTemp]);

  if (temps[0]) {
    var spread = Math.max(18, maxValue - minValue) / height * 12;
    distribute(lastValue, ['temp', 'hum', 'set'], spread);
  }

  var data = [
    {
      name: labels.temp,
      values: temps,
      labelValue: lastValue['temp']
    },
    {
      name: labels.hum,
      values: hums,
      labelValue: lastValue['hum']
    },
    {
      name: labels.setTemp,
      values: setTemps,
      labelValue: lastValue['set']
    }
  ];

  x.domain(d3.extent(data[0].values, function(d) { return d.time; }));
  y.domain([minValue - 5, maxValue + 5]);

  // Draw the axes
  svg.append('g')
      .attr('class', 'x axis')
      .attr('transform', 'translate(0,' + height + ')')
      .call(xAxis);

  svg.append('g')
      .attr('class', 'y axis')
      .call(yAxis)

  // Draw the grid
  svg.append('g')
      .attr('class', 'grid')
      .call(yAxis
          .tickSize(-width, 0, 0)
          .tickFormat('')
      )

  // Draw the data
  var series = svg.selectAll('.series')
      .data(data)
      .enter().append('g')
      .attr('class', 'series');

  series.append('path')
      .attr('class', 'line')
      .attr('d', function(d) { return line(d.values); })
      .style('stroke', function(d) { return color(d.name); });

  // Data is in reverse time order, so position text next to first item in array
  if (temps.length > 0) {
    series.append('text')
        .datum(function(d) { return {name: d.name, time: d.values[0].time, value: d.labelValue}; })
        .attr('transform', function(d) {
          return 'translate(' + x(d.time) + ',' + y(d.value) + ')';
        })
        .attr('x', 3)
        .attr('dy', '.35em')
        .style('font-size', '10px')
        .text(function(d) { return d.name; });
  }

  // Title and subtitles
  var title = $scope.info.title || 'Temperature & Humidity';
  if (temps.length < 1) {
    title = 'No data to display';
  }
  svg.append('text')
      .attr('x', (width / 2))
      .attr('y', 0)
      .attr('text-anchor', 'middle')
      .style('font-size', '16px')
      .text(title);
  svg.append('text')
      .attr('x', (width / 2))
      .attr('y', 18)
      .attr('text-anchor', 'middle')
      .style('font-size', '12px')
      .text(lastValue['time'].toLocaleString());
  var line3 = 'Heat: ' + ($scope.info.heat ? 'on' : 'off') + ' - Hold: ' + ($scope.info.hold ? 'on' : 'off');
  svg.append('text')
      .attr('x', (width / 2))
      .attr('y', 32)
      .attr('text-anchor', 'middle')
      .style('font-size', '12px')
      .text(line3);

}]);

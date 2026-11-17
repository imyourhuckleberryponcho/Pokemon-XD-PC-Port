-- halves the damage of every hit
on("battle.damage", function(event)
  return event.damage // 2
end)
